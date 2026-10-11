// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.h"

namespace Perimortem::Memory::Allocator {

struct Allocation {
  U8* ptr = nullptr;
  Count capacity = 0;
};

constexpr auto geometric_capacity(Count current, Count required) -> Count {
  constexpr Count initial_capacity = 32;
  const Count doubled = current > Count(-1) / 2 ? Count(-1) : current * 2;
  const Count grown = doubled > initial_capacity ? doubled : initial_capacity;
  return grown > required ? grown : required;
}

}  // namespace Perimortem::Memory::Allocator
