// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/access/bytes.h"
#include "perimortem/core/hash.hpp"
#include "perimortem/core/view/bytes.h"
#include "perimortem/memory/allocator/arena.hpp"

namespace Perimortem::Memory::Managed {

// Managed::Bytes stores contiguous bytes in an Arena supplied by its caller.
// Growth and reset select a new block without reclaiming the previous one, so
// views into an old block remain readable until the Arena resets or is
// destroyed. Those views do not follow writes after Bytes switches blocks, and
// Bytes does not extend the Arena's lifetime.
class Bytes {
 public:
  static constexpr Count start_capacity = 32;
  static constexpr Count growth_factor = 2;

  EXPORTED(PERIMORTEM) Bytes(
      const Bytes& rhs,
      Count reserved_capacity = start_capacity);
  EXPORTED(PERIMORTEM) Bytes(Bytes&& rhs);
  EXPORTED(PERIMORTEM) Bytes(Allocator::Arena& arena);
  EXPORTED(PERIMORTEM) Bytes(Allocator::Arena& arena, Core::View::Bytes view);

  constexpr operator Core::View::Bytes() const { return get_view(); }
  constexpr operator Core::Access::Bytes() { return get_access(); }

  EXPORTED(PERIMORTEM) auto reset(Count reserved_capacity = start_capacity)
      -> void;

  EXPORTED(PERIMORTEM) auto resize(Count new_size) -> void;
  EXPORTED(PERIMORTEM) auto ensure_capacity(Count required_bytes) -> void;

  EXPORTED(PERIMORTEM) auto append(U8 byte) -> void;
  EXPORTED(PERIMORTEM) auto append(U8 byte, Count amount) -> void;
  EXPORTED(PERIMORTEM) auto concat(Core::View::Bytes view) -> void;
  // Replaces the current bytes with a copy in this object's Arena. The source
  // View may use dynamic storage or another Arena.
  EXPORTED(PERIMORTEM) auto copy(Core::View::Bytes view) -> void;

  EXPORTED(PERIMORTEM) auto convert(U8 source, U8 target) -> void;

  constexpr auto operator[](Count index) const -> U8 {
    if (index >= size) {
      return 0;
    }

    return source_block[index];
  }

  constexpr auto at(Count index) const -> U8 {
    if (index >= size) {
      return 0;
    }

    return source_block[index];
  }

  constexpr auto get_size() const -> Count { return size; }
  constexpr auto get_capacity() const -> Count { return capacity; }
  constexpr auto get_view() const -> const Core::View::Bytes {
    return Core::View::Bytes(source_block, size);
  }

  constexpr auto get_data() const -> const U8* { return source_block; }
  constexpr auto get_access() -> Core::Access::Bytes {
    return Core::Access::Bytes(source_block, size);
  }

  constexpr auto get_arena() const -> Allocator::Arena& { return arena; }

  constexpr auto hash() const -> U64 {
    return Core::Hash(get_view()).get_value();
  }

 private:
  Allocator::Arena& arena;
  U8* source_block;
  Count size;
  Count capacity;
};

}  // namespace Perimortem::Memory::Managed
