// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.h"

namespace Perimortem::Memory::Allocator {

// The thread local runtime allocator used by Perimortem containers and workers.
//
// Each thread caches its own allocations in power of two size classes. Thread
// exit releases every slab owned by that cache.
//
// Stable allocation and remittance patterns maximize reuse while varied
// workloads continue through the same size classes.
class Bibliotheca {
 public:
  // Every corpus begins after one cache line sized Preface. Slab pages preserve
  // that boundary, so returned pointers support values with alignment no
  // greater than 64 bytes.
  static constexpr auto allocation_alignment = 64;

  struct Allocation {
    U8* ptr;
    Count capacity;
  };

  // Creates a free entry which can be used.
  static EXPORTED(PERIMORTEM) auto check_out(Count requested_bytes)
      -> Allocation;

  // Adds a reservation to the block.
  static EXPORTED(PERIMORTEM) auto reserve(U8* entry) -> Count;

  // Returns the number of active reservations on the block.
  static EXPORTED(PERIMORTEM) auto reservation_count(U8* entry) -> Count;

  // Returns the usable byte capacity selected when this block was checked out.
  // An empty entry has zero capacity.
  static EXPORTED(PERIMORTEM) auto capacity(U8* entry) -> Count;

  // Removes a reservation from the block.
  // If the number of reservations is zero then the block is checked in to the
  // Bibliotheca for future use.
  static EXPORTED(PERIMORTEM) auto remit(U8* entry) -> Count;

  // Methods for analyzing the state of the Bibliotheca.
  static EXPORTED(PERIMORTEM) auto reserved_memory() -> Count;
  static EXPORTED(PERIMORTEM) auto free_memory() -> Count;
  static EXPORTED(PERIMORTEM) auto allocated_memory() -> Count;
  static EXPORTED(PERIMORTEM) auto check_out_requests() -> Count;
  static EXPORTED(PERIMORTEM) auto allocation_requests() -> Count;
  static EXPORTED(PERIMORTEM) auto slab_requests() -> Count;
};

}  // namespace Perimortem::Memory::Allocator
