// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/memory/lifetime.hpp"

#include "perimortem/core/access/bytes.hpp"
#include "perimortem/core/access/vector.hpp"
#include "perimortem/core/data.hpp"
#include "perimortem/core/view/bytes.hpp"

namespace Perimortem::Memory::Allocator {

// Allocates objects whose access ends together when the Arena resets or is
// destroyed, allowing fast allocation and bulk storage reclamation.
//
// An arena avoids the bookkeeping overhead of an allocation header for every
// object. This makes it useful when many small objects must be created quickly
// and discarded together, such as a deserialized JSON document or one compiler
// transaction.
//
// `construct()` begins a C++ object lifetime in Arena storage. `Arena::reset()`
// ends every access promise without calling individual destructors. A later
// allocation can reuse retained storage and thereby end the former C++ object
// lifetime. Arena values cannot depend on individual destructors to release
// separate allocations, handles or Bibliotheca blocks. Rented Bibliotheca
// storage must therefore be remitted before the Arena is reset or destroyed.
//
// Arena has a stable identity. Objects and Managed containers retain its
// address, so copying or moving an Arena would separate those borrowers from
// the storage lifetime they joined.
class Arena {
 public:
  // Ordinary requests share 32 KiB pages. Each page includes the link used to
  // return earlier pages. A larger request receives a dedicated larger page.
  static constexpr U64 page_size = (1 << 15);
  static constexpr U64 arena_alignment = sizeof(Count);

  EXPORTED(PERIMORTEM) Arena();
  EXPORTED(PERIMORTEM) ~Arena();
  Arena(const Arena&) = delete;
  Arena(Arena&&) = delete;
  auto operator=(const Arena&) -> Arena& = delete;
  auto operator=(Arena&&) -> Arena& = delete;

  inline auto allocate(Count bytes_requested) -> Core::Access::Bytes {
    // Fetch a new page if we are full due to either running out of our current
    // page, or needing to allocate an object larger than our page size.
    //
    // An arena favors cheap allocation over page demotion. A long lived arena
    // can therefore retain an unusually large page after one large request.
    // Use it for bounded transactions rather than an unbounded object cache.
    if (usage > page_size || bytes_requested > page_size - usage) {
      fetch_page(bytes_requested);
    }

    // Round the next cursor to `arena_alignment`. Every returned block then
    // meets that alignment, while `reserve` rejects types requiring stricter
    // alignment.
    U8* root = rented_block + usage;
    usage = Core::Data::align<arena_alignment>(usage + bytes_requested);
    return Core::Access::Bytes(root[0], bytes_requested);
  }

  // Reserves storage for one object without beginning its lifetime.
  template <typename type>
  auto reserve() -> type& {
    static_assert(alignof(type) <= arena_alignment);
    return *Core::Data::cast<type>(allocate(sizeof(type)).get_data());
  }

  // Reserves aligned storage for `size` values without beginning their
  // lifetimes. The caller constructs every value before access.
  template <typename type>
  auto reserve(Count size) -> Core::Access::Vector<type> {
    static_assert(alignof(type) <= arena_alignment);
    // Keep an overflowing element count on the allocation failure path rather
    // than returning a small buffer with a large advertised range.
    const Count bytes =
        size > Count(-1) / sizeof(type) ? Count(-1) : sizeof(type) * size;
    auto ptr = allocate(bytes).get_data();
    auto return_access = [](Core::Access::Vector<type>& access) {
      return access;
    };

    auto unreachable = []() -> Core::Access::Vector<type> {
      __builtin_unreachable();
    };

    return Core::Access::Vector<type>::create(Core::Data::cast<type>(ptr), size)
        .visit(return_access, unreachable);
  }

  // Allocates `sizeof(type)` bytes from this Arena and begins one value
  // lifetime with its default constructor. Access ends when the Arena resets or
  // is destroyed.
  template <typename type>
  auto construct() -> type& {
    static_assert(alignof(type) <= arena_alignment);
    U8* ptr = allocate(sizeof(type)).get_data();
    return Toolchain::Memory::Lifetime::construct<type>(ptr);
  }

  // Allocates `sizeof(type)` bytes from this Arena and begins one value
  // lifetime by copying `source`. Allocation completes before reading `source`,
  // which may reside in an earlier Arena block.
  template <typename type>
  auto construct(const type& source) -> type&
    requires(__is_constructible(type, const type&))
  {
    return construct_from<type>([&source] { return type(source); });
  }

  // Allocates `sizeof(type)` bytes from this Arena and begins one value
  // lifetime by moving from `source`. Allocation completes before the source
  // state changes.
  template <typename type>
  auto construct(type&& source) -> type&
    requires(__is_constructible(type, type &&))
  {
    return construct_from<type>(
        [&source] { return type(Core::Data::take(source)); });
  }

  // Allocates one object from the exact value returned by `factory`.
  //
  // Guaranteed copy elision lets the factory keep constructor access while the
  // object begins its lifetime directly in arena storage. This supports
  // immovable objects without making their constructors public.
  template <typename type, typename factory_type>
  auto construct_from(factory_type factory) -> type& {
    static_assert(alignof(type) <= arena_alignment);
    static_assert(__is_same(type, decltype(factory())));
    U8* ptr = allocate(sizeof(type)).get_data();
    return Toolchain::Memory::Lifetime::construct<type>(
        ptr, Core::Data::take(factory));
  }

  // Copies every source value into this Arena. The returned View remains
  // accessible until this Arena resets or is destroyed.
  auto copy(Core::View::Bytes source) -> Core::View::Bytes {
    if (source.is_empty()) {
      return Perimortem::Core::View::Bytes();
    }

    auto access = allocate(source.get_size());
    Core::Data::copy(access.get_data(), source.get_data(), source.get_size());
    return access;
  }

  // Invalidates every pointer and reference obtained from this Arena. No object
  // destructor runs. The retained page may be reused by the next allocation,
  // so former values cannot be inspected after return.
  EXPORTED(PERIMORTEM) auto reset() -> void;

 private:
  // `allocate` keeps the bump path inline. Page acquisition remains out of line
  // so consumers share one implementation for checking out and remitting
  // Bibliotheca pages.
  EXPORTED(PERIMORTEM) auto fetch_page(Count bytes_requested) -> void;

  U8* rented_block;
  Count usage;
};

}  // namespace Perimortem::Memory::Allocator
