// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/memory/allocation.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/container/vector.hpp"

namespace Perimortem::Memory::Managed {

// Managed Vector stores values that share one Arena lifetime. Growth constructs
// current elements in a new block while old elements remain alive at their
// previous addresses until the Arena resets. An old pointer retains the state
// left by that copy or move and does not follow later vector writes.
//
// Clearing, replacement and destruction invoke no element destructors. Values
// stored here release no independently owned resources and retain only state
// governed by the same Arena.
//
// Copies construct independent current blocks. Copy construction uses the
// source Arena. Copy assignment retains the destination Arena, while move
// assignment adopts the source Arena together with its storage.
template <typename type>
class Vector final : public Container::Vector<Vector<type>, type> {
  using Base = Container::Vector<Vector<type>, type>;
  friend Base;

 public:
  Vector() = delete;
  // The Arena establishes every element's storage lifetime. Explicit
  // construction requires callers to name that binding at the call site.
  constexpr explicit Vector(Allocator::Arena& arena) : arena(&arena) {
    reset();
  }

  Vector(const Vector& rhs) : arena(rhs.arena) {
    reset(rhs.get_size());
    Base::copy_from(rhs);
  }

  constexpr Vector(Vector&& rhs) : arena(rhs.arena) {
    Base::take(static_cast<Base&&>(rhs));
  }

  constexpr ~Vector() = default;

  auto operator=(const Vector& rhs) -> Vector& {
    if (this != &rhs) {
      Base::clear();
      Base::copy_from(rhs);
    }

    return *this;
  }

  auto operator=(Vector&& rhs) -> Vector& {
    if (this != &rhs) {
      Base::reset_storage();
      arena = rhs.arena;
      Base::take(static_cast<Base&&>(rhs));
    }

    return *this;
  }

  auto prepend(const type& data) -> void { Base::prepend_copy(data); }

  constexpr auto reset(Count reserved_capacity = initial_capacity) -> void {
    const Count requested = reserved_capacity < initial_capacity
                                ? initial_capacity
                                : reserved_capacity;
    Base::reset_storage(requested);
  }

  constexpr auto get_arena() const -> Allocator::Arena& { return *arena; }

 private:
  static constexpr Count initial_capacity = 8;

  // Geometric growth limits the Arena blocks and retained object copies left by
  // repeated insertions. The first reservation preserves the established small
  // temporary collection path.
  static constexpr auto next_capacity(Count current, Count required) -> Count {
    const Count doubled = current > Count(-1) / 2 ? Count(-1) : current * 2;
    const Count grown = doubled > initial_capacity ? doubled : initial_capacity;
    return grown > required ? grown : required;
  }

  auto allocate(Count requested_capacity) -> Allocation<type> {
    auto block = arena->reserve<type>(requested_capacity);
    return Allocation<type>(block.get_data(), block.get_size());
  }

  static constexpr auto adopt(Allocation<type>&&) -> void {}

  template <typename factory_type>
  static constexpr auto construct(type* destination, factory_type&& factory)
      -> type& {
    return Toolchain::Memory::Lifetime::construct<type>(
        destination, static_cast<factory_type&&>(factory));
  }

  // Copyable values preserve the old object that Arena keeps accessible after
  // growth. Exclusive ownership instead moves because no independent value can
  // remain at the former address.
  static constexpr auto relocate(type* destination, type& source) -> void {
    if constexpr (__is_constructible(type, type&)) {
      construct(destination, [&source] { return type(source); });
    } else {
      construct(
          destination, [&source] { return type(static_cast<type&&>(source)); });
    }
  }

  // Reusing a slot ends its former C++ object lifetime without invoking that
  // object's destructor. Arena reset invalidates all access together, and the
  // Managed contract excludes independently owned resources.
  static constexpr auto replace(type* destination, type& source) -> void {
    relocate(destination, source);
  }

  // Removal changes the visible sequence but leaves its former storage and
  // object lifetime under the Arena. Whole storage release follows the same
  // rule until Arena reset or later storage reuse.
  static constexpr auto retire(type*, Count) -> void {}
  static constexpr auto release(type*, Count) -> void {}

  Allocator::Arena* arena;
};

}  // namespace Perimortem::Memory::Managed
