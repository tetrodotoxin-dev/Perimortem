// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/data.hpp"
#include "perimortem/memory/allocation.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/container/bytes.hpp"

namespace Perimortem::Memory::Managed {

// Managed Bytes allocates every attached block from its recorded Arena, which
// must therefore be supplied during construction. Arena bump allocation makes
// growth fast by trading independent release for a shared lifetime. Every View
// remains accessible only while that Arena is alive. After mutation, an earlier
// View has no guaranteed relationship to the current values because the
// operation may reuse or replace the block. `reset()` detaches the Bytes object
// while earlier Views continue to designate their former block until Arena
// reset.
//
// Copy construction uses the source Arena. Assignment retains the destination
// Arena and materializes an independent copy of the source values there. Move
// construction transfers the source Arena and current allocation, leaving the
// source empty. Arena reset invalidates every allocation together.
class Bytes final : public Container::Bytes<Bytes> {
  using Base = Container::Bytes<Bytes>;
  friend Base;

 public:
  Bytes() = delete;
  // An Arena selects the allocation and lifetime policy rather than supplying
  // byte values. Explicit construction prevents an Arena argument from
  // implicitly creating an empty Bytes object.
  constexpr explicit Bytes(Allocator::Arena& selected_arena)
      : arena(selected_arena) {}
  Bytes(Allocator::Arena& selected_arena, Count reserved_capacity)
      : arena(selected_arena) {
    Base::ensure_capacity(reserved_capacity);
  }

  Bytes(Allocator::Arena& selected_arena, Core::View::Bytes view)
      : arena(selected_arena) {
    Base::copy(view);
  }

  Bytes(Allocator::Arena& selected_arena, const Bytes& rhs)
      : arena(selected_arena) {
    Base::copy(rhs.get_view());
  }

  Bytes(const Bytes& rhs) : arena(rhs.arena) { Base::copy(rhs.get_view()); }

  constexpr Bytes(Bytes&& rhs) : arena(rhs.arena) {
    Base::take(Core::Data::take(rhs));
  }

  constexpr ~Bytes() = default;

  auto operator=(Core::View::Bytes view) -> Bytes& {
    Base::copy(view);
    return *this;
  }

  auto operator=(const Bytes& rhs) -> Bytes& {
    if (this != &rhs) {
      Base::copy(rhs.get_view());
    }

    return *this;
  }

  constexpr auto get_arena() const -> Allocator::Arena& { return arena; }

 private:
  // Arena cannot extend an existing block. Geometric growth reduces retained
  // blocks and copying across repeated appends.
  static constexpr auto next_capacity(Count current, Count required) -> Count {
    constexpr Count initial_capacity = 32;
    const Count doubled = current > Count(-1) / 2 ? Count(-1) : current * 2;
    const Count grown = doubled > initial_capacity ? doubled : initial_capacity;
    return grown > required ? grown : required;
  }

  auto allocate(Count requested_bytes) -> Allocation<U8> {
    auto block = arena.allocate(requested_bytes);
    return Allocation<U8>(block.get_data(), block.get_size());
  }

  static constexpr auto release(U8*) -> void {}

  Allocator::Arena& arena;
};

}  // namespace Perimortem::Memory::Managed
