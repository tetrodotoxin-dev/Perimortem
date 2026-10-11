// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/view/bytes.h"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/thread/bytes.hpp"

namespace Perimortem::Serialization {

class Base64 {
 public:
  static EXPORTED(PERIMORTEM) auto decode(Core::View::Bytes source)
      -> Memory::Thread::Bytes;
  static EXPORTED(PERIMORTEM) auto decode(
      Memory::Allocator::Arena& arena,
      Core::View::Bytes source) -> Core::View::Bytes;

  static EXPORTED(PERIMORTEM) auto encode(Core::View::Bytes source)
      -> Memory::Thread::Bytes;
  static EXPORTED(PERIMORTEM) auto encode(
      Memory::Allocator::Arena& arena,
      Core::View::Bytes source) -> Core::View::Bytes;
};

}  // namespace Perimortem::Serialization
