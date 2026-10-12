// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include <stddef.h>

#include "perimortem/memory/allocation.hpp"

namespace Perimortem::Memory::Allocator {

// Global owns untyped storage obtained from the process heap. Allocations can
// be released from a different thread, although concurrent access to their
// contents still requires synchronization.
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
