// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include <stddef.h>

#include "perimortem/memory/allocation.hpp"

namespace Perimortem::Memory::Allocator {

// Global obtains untyped storage from the process heap. `release` can return an
// allocation from a different thread, although concurrent access to its bytes
// still requires synchronization.
class Global {
 public:
  // The process heap supports every fundamental alignment. Typed containers
  // reject values with a stronger requirement before requesting their bytes.
  static constexpr Count allocation_alignment = alignof(max_align_t);

  static EXPORTED(PERIMORTEM) auto allocate(Count requested_bytes)
      -> Perimortem::Memory::Allocation<U8>;
  static EXPORTED(PERIMORTEM) auto release(U8* data) -> void;
};

}  // namespace Perimortem::Memory::Allocator
