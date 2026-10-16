// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/allocator/bibliotheca.hpp"

#ifdef TOOLCHAIN_WASM
#include <stdlib.h>
#endif

#ifdef TOOLCHAIN_WINDOWS
#include <windows.h>
#endif

#ifdef TOOLCHAIN_LINUX
#include <linux/mman.h>
#include <sys/mman.h>
#endif

#include "perimortem/core/data.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/scalar.hpp"
#include "perimortem/core/view/null_terminated.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;

// Returns the exponent of the smallest power of two that can contain `value`.
// Subtracting one keeps exact powers in their current size class while values
// between powers round up.
constexpr auto log2_pre_shift(U64 value) -> U64 {
  return 64 - __builtin_clzg(value - 1, S32(sizeof(U64) * 8));
}

// If Count is 64 bits then limit us to some level below the 256 TB limits.
// The upper radix is exclusive so the largest block holds 32 GiB.
static constexpr U8 max_radix = sizeof(Count) * 8 > 32 ? 36 : sizeof(Count) * 8;
static constexpr U8 min_radix = log2_pre_shift(64);
static constexpr Count min_size = (1 << min_radix);
static constexpr Count radix_range = max_radix - min_radix;

class alignas(64) Slab {
 public:
  static constexpr auto kilobytes_4 = 1 << 12;
  static constexpr auto megabytes_2 = kilobytes_4 << 9;

  // 32 MB chunks + 1 extra 2 MB chunk
  // The extra 2 MB chunk gives us wiggle room for all of the slab and preface
  // overhead which helps break up some particularly poor performing allocation
  // chunking based on Bibliotheca access patterns.
  static constexpr Count allocator_size = (megabytes_2 << 4) + megabytes_2;
  static auto get(Count block_size) -> Slab* {
    Count size = allocator_size;
    void* mapped_memory = nullptr;

#ifdef TOOLCHAIN_WASM

    // Browser linear memory uses aligned allocations rather than mapped pages.
    // Each returned pointer still begins one Slab allocation that
    // `Slab::release` later returns as a complete unit.
    size = Data::align<64>(
        block_size > allocator_size ? block_size : allocator_size);
    // Count remains a 64 bit quantity on Wasm32. The allocator boundary accepts
    // a CppSize so every Slab records the exact size passed to aligned_alloc.
    if (size > Count(CppSize(-1))) [[unlikely]] {
      Diagnostics::Log::fatal("Requested slab exceeds the address space."_view);
    }

    // The Bibliotheca requires 64 byte aligned memory which Wasm32 doesn't
    // guarantee so we need to use aligned_alloc explicitly.
    mapped_memory = aligned_alloc(64, size);
    if (!mapped_memory) [[unlikely]] {
      Diagnostics::Log::fatal("Unable to allocate a runtime slab."_view);
    }

#elifdef TOOLCHAIN_WINDOWS

    size = Data::align<megabytes_2>(Scalar::max(block_size, allocator_size));
    mapped_memory = VirtualAlloc(
        nullptr, CppSize(size), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!mapped_memory) [[unlikely]] {
      Diagnostics::Log::fatal("Unable to allocate a runtime slab."_view);
    }

#elifdef TOOLCHAIN_LINUX

    // Large requests round the slab size up to a multiple of 2MB. The resulting
    // extent is divisible by either 2MB huge pages or 4KB fallback pages.
    // Demotion eventually makes the unused tail available to smaller
    // allocations.
    if (size < block_size) {
      size = Data::align<megabytes_2>(block_size);
    }

    auto constexpr page_access = PROT_READ | PROT_WRITE;
    auto constexpr page_flags = MAP_PRIVATE | MAP_ANONYMOUS;
    // Request explicit 2 MB pages. A 1 GB page exceeds the ordinary slab
    // workload and depends on a much scarcer reserved page pool.
    auto constexpr huge_page = MAP_HUGETLB | MAP_HUGE_2MB;
    mapped_memory =
        mmap(nullptr, size, page_access, page_flags | huge_page, -1, 0);

    // `MAP_HUGETLB` draws from the reserved hugetlb pool and is independent of
    // transparent huge pages. A missing 2MB pool, exhausted pool, or lacking
    // permission rejects this request, so fall back to ordinary anonymous
    // pages.
    //
    // Failure of the ordinary mapping leaves no lower allocation mechanism to
    // delegate to without replacing the slab allocator with a `malloc` wrapper.
    if (mapped_memory == MAP_FAILED) {
      mapped_memory = mmap(nullptr, size, page_access, page_flags, -1, 0);

      if (mapped_memory == MAP_FAILED) [[unlikely]] {
        Diagnostics::Log::fatal(
            "Unable to map a new slab of memory for the runtime."_view);
      }

      // Mark the fallback as eligible on hosts that restrict transparent huge
      // pages to advised ranges. Failed advice leaves the ordinary mapping
      // valid, so it does not turn an optional optimization into allocation
      // failure.
      madvise(mapped_memory, size, MADV_HUGEPAGE);
    }

#else

    Diagnostics::Log::fatal(
        "Bibliotheca is unable to allocate memory from this system."_view);

#endif

    Slab* slab = Data::cast<Slab>(mapped_memory);
    slab->mapped_size = size;
    slab->ancestor = nullptr;
    slab->bump_ptr = sizeof(Slab);
    return slab;
  }

  static auto release(Slab* slab) -> Bool {

#ifdef TOOLCHAIN_WASM

    // Wasm slabs come from `malloc` rather than a virtual memory mapping.
    // Return the original pointer directly to `free`.
    free(slab);
    return True;

#elif defined(TOOLCHAIN_WINDOWS)

    if (!VirtualFree(slab, 0, MEM_RELEASE)) {
      Diagnostics::Log::error(
          "Bibliotheca failed to release a memory slab."_view);
      return False;
    }

    return True;

#elif defined(TOOLCHAIN_LINUX)

    auto success = munmap(slab, slab->mapped_size);
    if (success != 0) {
      Diagnostics::Log::error(
          "Bibliotheca failed to unmap a memory slab."_view);
      return False;
    }

    return True;

#else

    return False;

#endif
  }

  constexpr auto get_free_space() const -> Count {
    return mapped_size - bump_ptr;
  }

  constexpr auto get_ancestor() const -> Slab* { return ancestor; }
  constexpr auto set_ancestor(Slab* value) -> void { ancestor = value; }
  constexpr auto get_ancestor_slot() -> Slab*& { return ancestor; }

  // Bump allocation leaves size class selection and Preface construction to the
  // Librarian. Every requested extent is a multiple of 64 bytes, so `bump_ptr`
  // remains aligned without work on each allocation.
  //
  // Additional page boundary alignment slowed the common small block path and
  // is therefore left to slab sizing and the operating system.
  auto alloc(Count bytes) -> U8* {
    auto location = Data::cast<U8>(this) + bump_ptr;
    bump_ptr += bytes;
    return location;
  }

 private:
  Count mapped_size = 0;
  Count bump_ptr = 0;
  Slab* ancestor = nullptr;
};

// One cache line of slab metadata keeps the first bump allocation 64 byte
// aligned without per allocation padding.
static_assert(sizeof(Slab) == 64);

// Filling one cache line preserves the slab's 64 byte alignment for the corpus
// stored immediately after each Preface.
class Preface {
  friend Allocator::Bibliotheca;

 public:
  auto get_usable_bytes() const -> Count { return block_size; }

 private:
  // Counts active reservations that must be remitted before reuse.
  U64 reservations;
  // Records the byte capacity published through Allocation and excludes the
  // Preface header.
  U64 block_size;
  // Links blocks only while they reside on a size class free list.
  Preface* next;
  // The archive index is an invariant of the block so store it in the header.
  U64 archive_index;

#if PERI_DEBUG

  U64 block_stamp;
  [[maybe_unused]] U64 reserved[3];

#else

  [[maybe_unused]] U64 reserved[4];

#endif
};

static_assert(sizeof(Preface) == 64);
static_assert(sizeof(Preface) == Allocator::Bibliotheca::allocation_alignment);

class Archive {
 public:
  class Collection {
   public:
    Preface* initial_entry = nullptr;
    U32 reserved_blocks = 0;
    U32 free_blocks = 0;
  };

  Collection collections[radix_range];
  // These counters are cumulative for the current thread. They distinguish
  // total checkout traffic from slower paths after a free list miss. A request
  // contributes only to counters for the paths it uses.
  Count check_out_requests = 0;
  Count allocation_requests = 0;
  Count demote_requests = 0;
  Count slab_requests = 0;
};

// A POD archive avoids guarded dynamic initialization on the allocator fast
// path while retaining one independent cache for each thread. Limiting that
// metadata to half a KiB bounds the thread local footprint to eight cache lines
// while blocks cycle through its size classes.
thread_local static Archive secret_archive;

static_assert(sizeof(secret_archive) <= 512);

// Librarian retains the current thread's ordered slab inventory and releases it
// at thread exit. Archive keeps size class free lists and counters separately
// so checked out blocks do not participate in slab ordering.
class Librarian {
 public:
  Slab* inventory = nullptr;

  // Slabs stay ordered by descending free space so allocation consumes retained
  // capacity before mapping another slab. Reordering occurs only when the
  // active slab cannot satisfy a request, keeping the common bump path
  // independent of inventory length.
  auto manage_inventory(Count bytes) -> void {
    Slab* current = inventory;
    if (current->get_ancestor()) {
      secret_archive.demote_requests++;

      const auto free_space = current->get_free_space();

      // Keep slabs ordered by descending free space so later allocation starts
      // with the candidate most likely to satisfy it. Since the list is always
      // partially sorted we only have to do one linear `bubble sort` pass.
      auto parent = &inventory;
      while (current->get_ancestor() &&
             free_space < current->get_ancestor()->get_free_space()) {
        auto next = current->get_ancestor();
        current->set_ancestor(next->get_ancestor());
        next->set_ancestor(current);

        *parent = next;
        parent = &(*parent)->get_ancestor_slot();
      }
    }

    // The reordered head can satisfy this request without mapping another slab.
    if (bytes <= inventory->get_free_space()) [[likely]] {
      return;
    }

    // No retained slab can satisfy the request, so map a new inventory head.
    secret_archive.slab_requests++;

    auto new_slab = Slab::get(bytes);
    new_slab->set_ancestor(inventory);
    inventory = new_slab;
  }

  // `bytes` already includes both the requested corpus extent and its Preface.
  auto order_inventory(Count bytes) -> Preface* {
    secret_archive.allocation_requests++;

    if (bytes > inventory->get_free_space()) [[unlikely]] {
      manage_inventory(bytes);
    }

    Preface* entry = Data::cast<Preface>(inventory->alloc(bytes));
    return entry;
  }

  Librarian() {
    // Establish one inventory head so allocation never needs a null slab check.
    secret_archive.slab_requests++;
    inventory = Slab::get(Slab::allocator_size);
  }

  // Thread exit releases every slab, including storage for blocks that were not
  // remitted before the thread local cache ended.
  ~Librarian() {
    while (inventory) {
      auto entry = inventory;
      inventory = inventory->get_ancestor();
      Slab::release(entry);
    }
  }
};

constexpr auto calculate_archive_bucket(Count bytes) -> U8 {
  return log2_pre_shift(bytes > min_size ? bytes : min_size);
}

constexpr auto archive_page_width(U8 index) -> Count {
  return Count(1) << index;
}

// The preface fills the cache line immediately before the allocation.
auto corpus_to_preface(U8* entry) -> Preface* {
  return Data::cast<Preface>(entry) - 1;
}

auto preface_to_corpus(Preface* entry) -> U8* {
  return Data::cast<U8>(entry + 1);
}

auto Allocator::Bibliotheca::check_out(Count requested_bytes) -> Allocation {
  secret_archive.check_out_requests++;

  // Power of two classes map each request to one free list. Rounding guarantees
  // that the returned capacity is at least `requested_bytes`.
  const Count archive_bucket = calculate_archive_bucket(requested_bytes);
  if (archive_bucket >= max_radix) [[unlikely]] {
    Diagnostics::Log::fatal(
        "Requested allocation exceeds Bibliotheca's size classes."_view);
  }

  const Count actual_bytes =
      archive_page_width(archive_bucket) + sizeof(Preface);
  const Count archive_index = archive_bucket - min_radix;

  // An empty size class carves another block from the slab inventory.
  if (secret_archive.collections[archive_index].initial_entry == nullptr) {
    // Keeping the Librarian local to this branch delays its initial slab until
    // the thread's first free list miss.
    thread_local static Librarian dave;

    // Counting a block when carved keeps `reserved_memory` stable as the block
    // moves between checked out and free states.
    Preface* entry = dave.order_inventory(actual_bytes);
    secret_archive.collections[archive_index].reserved_blocks += 1;

#ifdef PERI_DEBUG

    if (entry == nullptr) [[unlikely]] {
      Diagnostics::Log::fatal(
          "Bibliotheca was unable to allocate memory from the OS"_view);
    }

    // Stamp a new header so `remit` can reject foreign or corrupted pointers.
    entry->block_stamp = 'PERI';

#endif

    // A new block remembers its archive for `remit` and begins with one
    // reservation. Usable capacity excludes the Preface stored before it.
    entry->archive_index = archive_index;
    entry->reservations = 1;
    entry->block_size = actual_bytes - sizeof(Preface);
    entry->next = nullptr;
    return Allocation(preface_to_corpus(entry), entry->get_usable_bytes());
  }

  Preface* entry = secret_archive.collections[archive_index].initial_entry;
  secret_archive.collections[archive_index].initial_entry = entry->next;
  secret_archive.collections[archive_index].free_blocks -= 1;

  // A free list entry retains its class and capacity. Clear its link and
  // restore one reservation before publishing the corpus again.
  entry->reservations = 1;
  entry->next = nullptr;
  return Allocation(preface_to_corpus(entry), entry->get_usable_bytes());
}

auto Allocator::Bibliotheca::reserve(U8* data) -> Count {
  auto entry = corpus_to_preface(data);
  return entry->reservations++;
}

auto Allocator::Bibliotheca::reservation_count(U8* data) -> Count {
  auto entry = corpus_to_preface(data);
  return entry->reservations;
}

auto Allocator::Bibliotheca::capacity(U8* data) -> Count {
  if (!data) {
    return 0;
  }

  return corpus_to_preface(data)->get_usable_bytes();
}

auto Allocator::Bibliotheca::remit(U8* data) -> Count {
  auto entry = corpus_to_preface(data);
  entry->reservations--;

#ifdef PERI_DEBUG

  // The debug stamp proves this pointer resolves to a Preface created by
  // Bibliotheca before its metadata is trusted.
  if (entry->block_stamp != 'PERI') [[unlikely]] {
    Diagnostics::Log::fatal(
        "Data returned to Bibliotheca had corrupted block_stamp, either from being invalid or from underflow."_view);
  }

  // Reservations must be nonzero before `remit`. Subtracting from zero wraps
  // U64 into this high range and makes a repeated remittance fatal.
  if (entry->reservations > 1u << 30) [[unlikely]] {
    Diagnostics::Log::fatal(
        "Reservations for the data remitted block overflowed."_view);
  }

#endif

  // The last remittance returns the block to its size class free list. A block
  // with remaining reservations stays checked out.
  if (entry->reservations == 0) {
    Count archive_index = entry->archive_index;

    entry->next = secret_archive.collections[archive_index].initial_entry;
    secret_archive.collections[archive_index].initial_entry = entry;
    secret_archive.collections[archive_index].free_blocks += 1;
    return 0;
  }

  return entry->reservations;
}

auto Allocator::Bibliotheca::reserved_memory() -> Count {
  Count total_size = 0;
  for (Count i = 0; i < radix_range; i++) {
    auto archive = secret_archive.collections[i];
    total_size += (Count(1) << (min_radix + i)) * archive.reserved_blocks;
  }

  return total_size;
}

auto Allocator::Bibliotheca::free_memory() -> Count {
  Count total_size = 0;
  for (Count i = 0; i < radix_range; i++) {
    auto archive = secret_archive.collections[i];
    total_size += (Count(1) << (min_radix + i)) * archive.free_blocks;
  }

  return total_size;
}

auto Allocator::Bibliotheca::allocated_memory() -> Count {
  auto reserved = reserved_memory();
  auto free = free_memory();
  return reserved - free;
}

auto Allocator::Bibliotheca::check_out_requests() -> Count {
  return secret_archive.check_out_requests;
}

auto Allocator::Bibliotheca::allocation_requests() -> Count {
  return secret_archive.allocation_requests;
}

auto Allocator::Bibliotheca::demote_requests() -> Count {
  return secret_archive.demote_requests;
}

auto Allocator::Bibliotheca::slab_requests() -> Count {
  return secret_archive.slab_requests;
}
