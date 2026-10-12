// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.h"

namespace Perimortem::Memory {

// Allocation couples a storage address with the usable element count published
// by its allocator. Pooling allocators can return more capacity than requested,
// so callers cannot reconstruct that extent from the original request. This
// value describes storage but does not release it. The supplying policy retains
// that authority.
template <typename type>
class Allocation {
 public:
  constexpr Allocation() = default;
  constexpr Allocation(type* data, Count capacity)
      : data(data), capacity(capacity) {}

  constexpr auto get_data() const -> type* { return data; }
  constexpr auto get_capacity() const -> Count { return capacity; }

 private:
  type* data = nullptr;
  Count capacity = 0;
};

}  // namespace Perimortem::Memory
