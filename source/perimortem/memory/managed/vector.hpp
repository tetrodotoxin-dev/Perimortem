// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/memory/lifetime.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/memory/allocation.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/container/vector.hpp"

namespace Perimortem::Memory::Managed {

// Managed Vector allocates every attached block from its recorded Arena, which
// must therefore be supplied during construction. Arena bump allocation makes
// growth fast by trading independent release for a shared lifetime. Every
// pointer and reference remains accessible only while that Arena is alive.
// Growth constructs a new current allocation without reclaiming the former
// block. A reference into the former block continues to designate the value
// left there by relocation and does not follow later Vector writes.
//
// `Arena::reset()` reclaims every block together. Clearing, replacement and
// destruction therefore invoke no value destructors. Value types cannot depend
// on individual destructors to release separate allocations, handles or
// Bibliotheca blocks. Every address retained by a value remains valid only
// under the same Arena. `Arena::reset()` invalidates every value pointer and
// reference and ends access to all values together.
//
// Copy construction uses the source Arena. Assignment retains the destination
// Arena and materializes an independent copy of the source values there. Move
// construction transfers the source Arena and current allocation without
// relocating values or invoking destructors, leaving the source empty. Calling
// `get_access()` on an allocation free Vector reserves an Arena block without
// changing size or constructing a current value. `Vector::reset()` detaches
// that block from the object, but only `Arena::reset()` reclaims it.
template <typename type>
class Vector final : public Container::Vector<Vector<type>, type> {
  using Base = Container::Vector<Vector<type>, type>;
  friend Base;

 public:
  Vector() = delete;
  // An Arena selects the allocation and value lifetime policy rather than
  // supplying values. Explicit construction prevents an Arena argument from
  // implicitly creating an empty Vector.
  constexpr explicit Vector(Allocator::Arena& selected_arena)
      : arena(selected_arena) {}

  Vector(Allocator::Arena& selected_arena, Count reserved_capacity)
      : arena(selected_arena) {
    Base::ensure_capacity(reserved_capacity);
  }

  Vector(Allocator::Arena& selected_arena, Core::View::Vector<type> source)
      : arena(selected_arena) {
    Base::copy_from(source);
  }

  Vector(Allocator::Arena& selected_arena, const Vector& source)
      : arena(selected_arena) {
    Base::copy_from(source);
  }

  Vector(const Vector& rhs) : arena(rhs.arena) { Base::copy_from(rhs); }

  constexpr Vector(Vector&& rhs) : arena(rhs.arena) {
    Base::take(Core::Data::take(rhs));
  }

  constexpr ~Vector() = default;

  auto operator=(const Vector& rhs) -> Vector& {
    if (this != &rhs) {
      Base::clear();
      Base::copy_from(rhs);
    }

    return *this;
  }

  auto operator=(Core::View::Vector<type> source) -> Vector&
    requires(__is_constructible(type, const type&))
  {
    // Materialize every source value before detaching this block because
    // `source` may reference this Vector. Assignment preserves the selected
    // Arena.
    Vector replacement(arena, source);
    Base::reset();
    Base::take(Core::Data::take(replacement));
    return *this;
  }

  constexpr auto get_arena() const -> Allocator::Arena& { return arena; }

 private:
  static constexpr Count initial_capacity = 8;

  // Arena cannot extend an existing block. Geometric growth reduces retained
  // blocks and value relocation across repeated insertions. The first
  // reservation preserves the established small temporary collection path.
  static constexpr auto next_capacity(Count current, Count required) -> Count {
    const Count doubled = current > Count(-1) / 2 ? Count(-1) : current * 2;
    const Count grown = doubled > initial_capacity ? doubled : initial_capacity;
    return grown > required ? grown : required;
  }

  auto allocate(Count requested_capacity) -> Allocation<type> {
    auto block = arena.reserve<type>(requested_capacity);
    return Allocation<type>(block.get_data(), block.get_size());
  }

  static constexpr auto adopt(Allocation<type>&&) -> void {}

  // A nested Managed value must use this Vector's Arena so
  // `Arena::reset()` on another Arena cannot invalidate data reachable through
  // the Vector. A value already using this Arena can move into `destination`. A
  // value from another Arena is copied into this Arena. Values without an Arena
  // contract use their ordinary construction path.
  template <typename selected_type>
  static constexpr Bool has_arena_copy =
      __is_constructible(
          selected_type,
          Allocator::Arena&,
          const selected_type&) &&
      requires(const selected_type& selected) { selected.get_arena(); };

  template <typename factory_type>
  constexpr auto construct(type* destination, factory_type factory) -> type& {
    if constexpr (has_arena_copy<type>) {
      type candidate = factory();
      return Toolchain::Memory::Lifetime::construct<type>(
          destination, [this, &candidate] {
            if (&candidate.get_arena() == &arena) {
              return type(Core::Data::take(candidate));
            }

            return type(arena, candidate);
          });
    }

    return Toolchain::Memory::Lifetime::construct<type>(
        destination, Core::Data::take(factory));
  }

  // Growth leaves the former Arena block accessible until `Arena::reset()`.
  // Copying constructs the current value in uninitialized storage at
  // `destination` without changing `source`. A move only value cannot be copied
  // into a second current value, so relocation move constructs `destination`
  // and leaves `source` alive in its moved from state.
  constexpr auto relocate(type* destination, type& source) -> void {
    if constexpr (__is_constructible(type, const type&)) {
      const type& copy_source = source;
      construct(destination, [&copy_source] { return type(copy_source); });
    } else {
      construct(
          destination, [&source] { return type(Core::Data::take(source)); });
    }
  }

  // Arena cannot retire one value independently. Reusing `destination` begins
  // the replacement lifetime without invoking the former value's destructor.
  // References to the former value no longer identify the current value.
  constexpr auto replace(type* destination, type& source) -> void {
    relocate(destination, source);
  }

  // Removal excludes a value from the Vector but leaves that value at its Arena
  // address. `Arena::reset()` or later reuse of the address ends access.
  static constexpr auto retire(type*, Count) -> void {}

  // `Arena::reset()` reclaims every block, so detaching Vector storage requires
  // no individual release operation.
  static constexpr auto release(type*, Count) -> void {}

  Allocator::Arena& arena;
};

}  // namespace Perimortem::Memory::Managed
