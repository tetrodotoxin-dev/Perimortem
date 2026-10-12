// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/memory/const_allocation.hpp"

namespace Perimortem::Memory::Const {

// Owns one value allocated through Toolchain during constant evaluation.
// Copies create independent values and moves transfer the allocation.
template <typename type>
class Object {
 public:
  consteval Object() = default;
  constexpr ~Object() = default;

  consteval Object(const type& value) : allocation(1) {
    *allocation.get_data() = value;
  }

  consteval Object(const Object& rhs) {
    if (rhs.get_data()) {
      allocation = Toolchain::Memory::ConstAllocation<type>(1);
      *allocation.get_data() = *rhs.get_data();
    }
  }

  consteval Object(Object&& rhs)
      : allocation(
            static_cast<Toolchain::Memory::ConstAllocation<type>&&>(
                rhs.allocation)) {}

  consteval auto operator=(const Object& rhs) -> Object& {
    if (this == &rhs) {
      return *this;
    }

    if (!rhs.get_data()) {
      allocation = Toolchain::Memory::ConstAllocation<type>();
      return *this;
    }

    if (!allocation.get_data()) {
      allocation = Toolchain::Memory::ConstAllocation<type>(1);
    }

    *allocation.get_data() = *rhs.get_data();
    return *this;
  }

  consteval auto operator=(Object&& rhs) -> Object& {
    allocation =
        static_cast<Toolchain::Memory::ConstAllocation<type>&&>(rhs.allocation);
    return *this;
  }

  constexpr auto get_data() const -> const type* {
    return allocation.get_data();
  }
  constexpr auto get_data() -> type* { return allocation.get_data(); }

 private:
  Toolchain::Memory::ConstAllocation<type> allocation;
};

}  // namespace Perimortem::Memory::Const
