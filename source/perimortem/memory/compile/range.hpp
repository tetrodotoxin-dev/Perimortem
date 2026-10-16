// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/compile/allocation.hpp"

#include "perimortem/core/data.hpp"

namespace Perimortem::Memory::Compile {

// Range provides fixed contiguous working storage to a constant expression.
// Construction begins every element lifetime. Copies receive independent
// storage, while moves preserve the addresses observed before the transfer.
// Its allocation cannot escape into a runtime value.
template <typename type>
class Range {
 public:
  consteval Range() = default;
  constexpr ~Range() = default;
  consteval Range(Count size) : allocation(size) {}

  consteval Range(const Range& rhs) : allocation(rhs.get_size()) {
    for (Count i = 0; i < get_size(); i++) {
      get_data()[i] = rhs.get_data()[i];
    }
  }

  consteval Range(Range&& rhs) : allocation(Core::Data::take(rhs.allocation)) {}

  consteval auto operator=(const Range& rhs) -> Range& {
    if (this == &rhs) {
      return *this;
    }

    if (rhs.get_size() != get_size()) {
      allocation = Toolchain::Compile::Allocation<type>(rhs.get_size());
    }

    for (Count i = 0; i < get_size(); i++) {
      get_data()[i] = rhs.get_data()[i];
    }

    return *this;
  }

  consteval auto operator=(Range&& rhs) -> Range& {
    allocation = Core::Data::take(rhs.allocation);
    return *this;
  }

  consteval auto operator[](Count index) const -> const type& {
    return get_data()[index];
  }

  consteval auto operator[](Count index) -> type& { return get_data()[index]; }

  constexpr auto get_data() const -> const type* {
    return allocation.get_data();
  }

  constexpr auto get_data() -> type* { return allocation.get_data(); }
  constexpr auto get_size() const -> Count { return allocation.get_capacity(); }

 private:
  Toolchain::Compile::Allocation<type> allocation;
};

}  // namespace Perimortem::Memory::Compile
