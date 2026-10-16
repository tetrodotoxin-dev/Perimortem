// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/memory/allocation.hpp"

namespace Perimortem::Memory::Allocator {

// The thread local runtime allocator used by Perimortem containers and workers.
//
// Each thread caches allocations in power of two size classes. Thread exit
// releases every slab retained by that cache.
//
// A block remains attached to the archive of the thread that checked it out.
// Every reservation and remittance for that block occurs on the same thread.
//
// Stable allocation and remittance patterns maximize reuse while varied
// workloads continue through the same size classes.
class Bibliotheca {
 public:
  // Every corpus begins after one cache line sized Preface. Slab pages preserve
  // that boundary, so returned pointers support values with alignment no
  // greater than 64 bytes.
  static constexpr auto allocation_alignment = 64;

  using Allocation = Perimortem::Memory::Allocation<U8>;

  // Returns an uninitialized block with at least `requested_bytes` usable bytes
  // and one active reservation. The selected size class can make capacity
  // larger than the request. The final `remit` recycles the block and ends
  // access through its pointer.
  static EXPORTED(PERIMORTEM) auto check_out(Count requested_bytes)
      -> Allocation;

  // Adds one reservation and returns the count that existed before the call.
  static EXPORTED(PERIMORTEM) auto reserve(U8* entry) -> Count;

  // Returns the active reservation count so a caller can require exclusive
  // access before mutating a block in place.
  static EXPORTED(PERIMORTEM) auto reservation_count(U8* entry) -> Count;

  // Returns the usable byte capacity selected when this block was checked out.
  // An empty entry has zero capacity.
  static EXPORTED(PERIMORTEM) auto capacity(U8* entry) -> Count;

  // Removes one reservation and returns the remaining count. Reaching zero
  // places the block on its size class free list, after which callers no longer
  // access it.
  static EXPORTED(PERIMORTEM) auto remit(U8* entry) -> Count;

  // Returns usable capacity in every block carved from this thread's slabs.
  // This includes both checked out and reusable blocks.
  static EXPORTED(PERIMORTEM) auto reserved_memory() -> Count;

  // Returns usable capacity in blocks available for another checkout by this
  // thread.
  static EXPORTED(PERIMORTEM) auto free_memory() -> Count;

  // Returns usable capacity in blocks currently checked out by this thread.
  static EXPORTED(PERIMORTEM) auto allocated_memory() -> Count;

  // Counts every `check_out` call against the current thread's archive,
  // including requests satisfied from its free lists.
  static EXPORTED(PERIMORTEM) auto check_out_requests() -> Count;

  // Returns the number of blocks carved from this thread's slabs after a free
  // list miss.
  static EXPORTED(PERIMORTEM) auto allocation_requests() -> Count;

  // Counts backing slabs obtained for the current thread, including its
  // initial Librarian slab and later inventory growth.
  static EXPORTED(PERIMORTEM) auto slab_requests() -> Count;
};

}  // namespace Perimortem::Memory::Allocator
