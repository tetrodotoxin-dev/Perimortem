// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/access/bytes.h"
#include "perimortem/core/hash.hpp"
#include "perimortem/core/view/bytes.h"

namespace Perimortem::Memory::Container {

// Bytes provides contiguous byte storage to a policy that defines allocation
// and release. The policy owns construction, copy and transfer contracts while
// this capability owns byte behavior and storage invariants.
//
// The null representation has no allocation, zero size and zero capacity. A
// policy that exposes default construction evaluates in constexpr O(1) time.
// Replacing any value with empty bytes performs no allocation, but that
// operation may retain or release existing storage according to its purpose.
// `clear` retains storage for reuse while `reset` releases policy ownership.
template <typename policy_type>
class Bytes {
 public:
  auto operator==(const Bytes& rhs) const -> Bool {
    return get_view() == rhs.get_view();
  }

  auto operator==(const Core::View::Bytes& rhs) const -> Bool {
    return get_view() == rhs;
  }

  constexpr operator Core::View::Bytes() const { return get_view(); }
  constexpr operator Core::Access::Bytes() { return get_access(); }

  EXPORTED(PERIMORTEM) auto append(U8 byte) -> void;
  EXPORTED(PERIMORTEM) auto append(U8 byte, Count amount) -> void;
  // Appends a copy of `view`. A View over the current visible bytes remains
  // supported when growth replaces its allocation.
  EXPORTED(PERIMORTEM) auto concat(Core::View::Bytes view) -> void;

  // Replaces the visible bytes with a copy. The source may overlap the current
  // allocation because replacement keeps those bytes valid until copying ends.
  EXPORTED(PERIMORTEM) auto copy(Core::View::Bytes view) -> void;

  // Changes the visible size while preserving retained bytes. Callers
  // initialize bytes exposed from previously unused capacity.
  EXPORTED(PERIMORTEM) auto resize(Count new_size) -> void;

  // Changes size without preserving storage outside the requested extent.
  EXPORTED(PERIMORTEM) auto forgetful_resize(Count required_size) -> void;

  // Replaces the visible bytes with a window over their previous contents.
  // Result index zero maps to previous index `start`. Positions outside the
  // previous range become zero, so a negative start adds leading padding and a
  // window past the end adds trailing padding. Storage is reused when
  // `new_size` fits capacity.
  EXPORTED(PERIMORTEM) auto reframe(S64 start, Count new_size) -> void;

  EXPORTED(PERIMORTEM) auto operator[](Count index) const -> U8;
  EXPORTED(PERIMORTEM) auto at(Count index) const -> U8;
  EXPORTED(PERIMORTEM) auto set(U8 target) -> void;
  EXPORTED(PERIMORTEM) auto convert(U8 source, U8 target) -> void;
  EXPORTED(PERIMORTEM) auto slice(Count start, Count size) const
      -> Core::View::Bytes;

  constexpr auto get_size() const -> Count { return size; }
  constexpr auto get_capacity() const -> Count { return capacity; }
  constexpr auto get_view() const -> Core::View::Bytes {
    return Core::View::Bytes(source_block, size);
  }

  constexpr auto get_access() -> Core::Access::Bytes {
    return Core::Access::Bytes(source_block, size);
  }

  auto hash() const -> U64 { return Core::Hash(get_view()).get_value(); }
  constexpr auto is_empty() const -> Bool { return size == 0; }

  EXPORTED(PERIMORTEM) auto clear() -> void;
  EXPORTED(PERIMORTEM) auto reset() -> void;
  EXPORTED(PERIMORTEM) auto ensure_capacity(Count required_size) -> void;

 protected:
  constexpr Bytes() = default;

  // Allocation reaches the derived policy only after wrapper construction has
  // established policy state. Release remains static because base destruction
  // follows the end of that state.
  constexpr ~Bytes() {
    if (source_block) {
      policy_type::release(source_block);
    }
  }

  constexpr auto take(Bytes&& rhs) -> void {
    source_block = rhs.source_block;
    size = rhs.size;
    capacity = rhs.capacity;
    rhs.source_block = nullptr;
    rhs.size = 0;
    rhs.capacity = 0;
  }

 private:
  constexpr auto get_policy() -> policy_type& {
    return static_cast<policy_type&>(*this);
  }

  U8* source_block = nullptr;
  Count size = 0;
  Count capacity = 0;
};

}  // namespace Perimortem::Memory::Container
