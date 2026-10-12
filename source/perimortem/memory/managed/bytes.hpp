// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/memory/allocation.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/container/bytes.hpp"

namespace Perimortem::Memory::Managed {

// Managed Bytes borrows one Arena and cannot be default constructed. Its fast
// bump allocation trades independent reclamation for the Arena's shared
// lifetime. A value remains within its Arena and cannot outlive that Arena.
//
// Managed policies compose only values owned by the same Arena. Arena reset
// ends those values together, so Managed containers do not invoke individual
// destructors or reclaim individual allocations.
class Bytes final : public Container::Bytes<Bytes> {
  using Base = Container::Bytes<Bytes>;
  friend Base;

 public:
  Bytes() = delete;
  // The Arena establishes the value's allocation and lifetime policy. Explicit
  // construction requires callers to name that binding even in the null state.
  constexpr explicit Bytes(Allocator::Arena& arena) : arena(&arena) {}
  Bytes(Allocator::Arena& arena, Count reserved_capacity) : arena(&arena) {
    Base::ensure_capacity(reserved_capacity);
  }

  Bytes(Allocator::Arena& arena, Core::View::Bytes view) : arena(&arena) {
    Base::copy(view);
  }

  Bytes(const Bytes& rhs) : arena(rhs.arena) { Base::copy(rhs.get_view()); }

  constexpr Bytes(Bytes&& rhs) : arena(rhs.arena) {
    Base::take(static_cast<Base&&>(rhs));
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

  auto operator=(Bytes&& rhs) -> Bytes& {
    if (this != &rhs) {
      Base::reset();
      arena = rhs.arena;
      Base::take(static_cast<Base&&>(rhs));
    }

    return *this;
  }

  constexpr auto get_arena() const -> Allocator::Arena& { return *arena; }

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
    auto block = arena->allocate(requested_bytes);
    return Allocation<U8>(block.get_data(), block.get_size());
  }

  static constexpr auto release(U8*) -> void {}

  Allocator::Arena* arena;
};

}  // namespace Perimortem::Memory::Managed
