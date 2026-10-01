// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/view/vector.hpp"
#include "perimortem/core/access/bytes.hpp"
#include "perimortem/core/option.hpp"
#include "perimortem/core/scalar.hpp"

namespace Perimortem::Core::Access {

// A borrowed read and write view of continuous data with possible endianness
// and structure. Indexed access selects the nearest endpoint when the index
// lies outside a nonempty view. An empty view returns None, and get_data leaves
// bounds to the caller.
//
// Vector data can be converted to Bytes data in order to interperet it
// at a byte level, however this is only valid in memory. To write and read
// binary data as vector data it should be serialized using
// `Perimortem::Serialization::Binary`.
template <typename type>
class Vector {
 public:
  using data_type = type;

  // Default to empty string.
  constexpr Vector() = default;
  constexpr Vector(const Vector&) = default;

  constexpr Vector(data_type* source, Count source_size)
      : source_block(source), size(source ? source_size : 0) {}

  template <Count N>
  constexpr Vector(data_type (&source)[N]) : source_block(source), size(N) {}

  constexpr auto at(Count index) -> Core::Option<data_type&> {
    if (is_empty()) [[unlikely]] {
      return {};
    }

    return source_block[index < size ? index : size - 1];
  }

  template <typename index_type>
    requires(
        __is_integral(index_type) && __is_signed(index_type) &&
        sizeof(index_type) <= sizeof(S64))
  constexpr auto at(index_type index) -> Core::Option<data_type&> {
    if (is_empty()) [[unlikely]] {
      return {};
    }

    if (index < 0) {
      return source_block[0];
    }

    Count coordinate = index;
    return at(coordinate);
  }

  constexpr auto operator[](Count index) -> Core::Option<data_type&> {
    return at(index);
  }

  template <typename index_type>
    requires(
        __is_integral(index_type) && __is_signed(index_type) &&
        sizeof(index_type) <= sizeof(S64))
  constexpr auto operator[](index_type index) -> Core::Option<data_type&> {
    return at(index);
  }

  constexpr auto slice(Count start, Count size = Count(-1)) const
      -> Access::Vector<data_type> {
    if (start >= get_size()) {
      return Access::Vector<data_type>();
    }

    return Access::Vector<data_type>(
        source_block + start, Core::Scalar::min(size, get_size() - start));
  };

  constexpr auto is_empty() const -> Bool { return size == 0; };
  constexpr auto get_size() const -> Count { return size; };
  constexpr auto get_data() -> data_type* { return source_block; };
  constexpr auto get_bytes() const -> Access::Bytes {
    return Access::Bytes((U8*)source_block, get_size() * sizeof(data_type));
  }

  constexpr auto get_view() const -> const View::Vector<data_type> {
    return View::Vector(source_block, get_size());
  }

 private:
  type* source_block = nullptr;
  Count size = 0;
};

}  // namespace Perimortem::Core::Access
