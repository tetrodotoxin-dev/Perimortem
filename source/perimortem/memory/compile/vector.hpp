// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/compile/allocation.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/memory/container/vector.hpp"

namespace Perimortem::Memory::Compile {

// `Compile::Vector` provides temporary values only during constant evaluation.
// Every capacity slot has a live value, so insertion and relocation assign
// existing objects rather than beginning individual lifetimes in raw storage.
// Immediate allocation prevents this policy from becoming a runtime path to
// `new`.
template <typename type>
class Vector final : public Container::Vector<Vector<type>, type> {
  using Base = Container::Vector<Vector<type>, type>;
  friend Base;

 public:
  consteval Vector() = default;

  consteval Vector(const Vector& rhs) { Base::copy_from(rhs); }

  consteval Vector(Vector&& rhs)
      : allocation(Core::Data::take(rhs.allocation)) {
    Base::take(Core::Data::take(rhs));
  }

  constexpr ~Vector() = default;

  consteval auto insert(type value) -> type& {
    return Base::insert(Core::Data::take(value));
  }

  consteval auto operator=(const Vector& rhs) -> Vector& {
    if (this != &rhs) {
      Base::clear();
      Base::copy_from(rhs);
    }

    return *this;
  }

  consteval auto operator=(Vector&& rhs) -> Vector& {
    if (this != &rhs) {
      Base::reset();
      allocation = Core::Data::take(rhs.allocation);
      Base::take(Core::Data::take(rhs));
    }

    return *this;
  }

 private:
  // `Toolchain::Compile::Allocation` returns the requested value count
  // exactly. Geometric growth limits repeated allocation during constant
  // evaluation.
  static constexpr auto next_capacity(Count current, Count required) -> Count {
    const Count doubled = current > Count(-1) / 2 ? Count(-1) : current * 2;
    return doubled > required ? doubled : required;
  }

  static consteval auto allocate(Count requested_capacity)
      -> Toolchain::Compile::Allocation<type> {
    return Toolchain::Compile::Allocation<type>(requested_capacity);
  }

  consteval auto adopt(Toolchain::Compile::Allocation<type>&& retained)
      -> void {
    allocation = Core::Data::take(retained);
  }

  template <typename factory_type>
  static consteval auto construct(type* destination, factory_type factory)
      -> type& {
    *destination = Core::Data::take(factory)();
    return *destination;
  }

  // `Toolchain::Compile::Allocation` keeps every capacity slot alive.
  // Moving from `source` into `destination` transfers allocations retained by
  // nested values before the old capacity is deleted. Copying could leave two
  // values referring to the same allocation.
  static consteval auto relocate(type* destination, type& source) -> void {
    *destination = Core::Data::take(source);
  }

  // Both locations already contain live values. Move assignment updates
  // `destination` without beginning another lifetime in the capacity slot.
  static consteval auto replace(type* destination, type& source) -> void {
    *destination = Core::Data::take(source);
  }

  // Every capacity slot remains live until the allocation is deleted. Restore
  // retired values to their default state instead of ending their lifetimes.
  static consteval auto retire(type* data, Count size) -> void {
    for (Count index = 0; index < size; index++) {
      data[index] = type();
    }
  }

  // `Toolchain::Compile::Allocation` spans the complete capacity. Replacing it
  // with an empty allocation destroys those capacity values and releases the
  // buffer without retiring each value through Vector.
  consteval auto release(type*, Count) -> void {
    allocation = Toolchain::Compile::Allocation<type>();
  }

  Toolchain::Compile::Allocation<type> allocation;
};

}  // namespace Perimortem::Memory::Compile
