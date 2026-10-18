// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/compile/allocation.hpp"

#include "perimortem/core/data.hpp"

namespace Perimortem::Memory::Compile {

// Object gives constant evaluated containers one separately allocated value
// whose address survives container growth. Every constructor is consteval, so
// neither the allocation nor a pointer into it can escape into runtime code.
//
// Default construction has no allocation. Copying a populated Object creates
// independent storage. Moving transfers the exact allocation so existing
// pointers retain their target and leaves the source empty and reusable.
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
      allocation = Toolchain::Compile::Allocation<type>(1);
      *allocation.get_data() = *rhs.get_data();
    }
  }

  consteval Object(Object&& rhs)
      : allocation(Core::Data::take(rhs.allocation)) {}

  consteval auto operator=(const Object& rhs) -> Object& {
    if (this == &rhs) {
      return *this;
    }

    if (!rhs.get_data()) {
      allocation = Toolchain::Compile::Allocation<type>();
      return *this;
    }

    if (!allocation.get_data()) {
      allocation = Toolchain::Compile::Allocation<type>(1);
    }

    *allocation.get_data() = *rhs.get_data();
    return *this;
  }

  consteval auto operator=(Object&& rhs) -> Object& {
    allocation = Core::Data::take(rhs.allocation);
    return *this;
  }

  constexpr auto get_data() const -> const type* {
    return allocation.get_data();
  }

  constexpr auto get_data() -> type* { return allocation.get_data(); }

 private:
  Toolchain::Compile::Allocation<type> allocation;
};

}  // namespace Perimortem::Memory::Compile
