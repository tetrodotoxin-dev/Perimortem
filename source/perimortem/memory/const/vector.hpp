// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/memory/const_allocation.hpp"

#include "perimortem/memory/container/vector.hpp"

namespace Perimortem::Memory::Const {

// Const Vector owns temporary values only during constant evaluation. Every
// capacity slot has a live value, so insertion and relocation assign existing
// objects rather than beginning individual lifetimes in raw storage. Immediate
// allocation prevents this policy from becoming a runtime path to `new`.
template <typename type>
class Vector final : public Container::Vector<Vector<type>, type> {
  using Base = Container::Vector<Vector<type>, type>;
  friend Base;

 public:
  consteval Vector() = default;

  consteval Vector(const Vector& rhs) { Base::copy_from(rhs); }

  consteval Vector(Vector&& rhs)
      : allocation(
            static_cast<Toolchain::Memory::ConstAllocation<type>&&>(
                rhs.allocation)) {
    Base::take(static_cast<Base&&>(rhs));
  }

  constexpr ~Vector() = default;

  consteval auto insert(type value) -> type& {
    return Base::emplace(static_cast<type&&>(value));
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
      Base::reset_storage();
      allocation = static_cast<Toolchain::Memory::ConstAllocation<type>&&>(
          rhs.allocation);
      Base::take(static_cast<Base&&>(rhs));
    }

    return *this;
  }

  consteval auto reset() -> void { Base::reset_storage(); }

 private:
  // ConstAllocation returns the requested element count exactly. Geometric
  // growth limits repeated allocation during constant evaluation.
  static constexpr auto next_capacity(Count current, Count required) -> Count {
    const Count doubled = current > Count(-1) / 2 ? Count(-1) : current * 2;
    return doubled > required ? doubled : required;
  }

  static consteval auto allocate(Count requested_capacity)
      -> Toolchain::Memory::ConstAllocation<type> {
    return Toolchain::Memory::ConstAllocation<type>(requested_capacity);
  }

  consteval auto adopt(Toolchain::Memory::ConstAllocation<type>&& retained)
      -> void {
    allocation =
        static_cast<Toolchain::Memory::ConstAllocation<type>&&>(retained);
  }

  template <typename factory_type>
  static consteval auto construct(type* destination, factory_type&& factory)
      -> type& {
    *destination = static_cast<factory_type&&>(factory)();
    return *destination;
  }

  // Moving transfers nested constant allocations before the old capacity is
  // deleted. Copying here would replace the stable pointees retained by users.
  static consteval auto relocate(type* destination, type& source) -> void {
    *destination = static_cast<type&&>(source);
  }

  static consteval auto replace(type* destination, type& source) -> void {
    *destination = static_cast<type&&>(source);
  }

  static consteval auto retire(type* data, Count size) -> void {
    for (Count index = 0; index < size; index++) {
      data[index] = type();
    }
  }

  consteval auto release(type*, Count) -> void {
    allocation = Toolchain::Memory::ConstAllocation<type>();
  }

  Toolchain::Memory::ConstAllocation<type> allocation;
};

}  // namespace Perimortem::Memory::Const
