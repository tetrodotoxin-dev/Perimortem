// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/view/bytes.hpp"
#include "perimortem/core/option.hpp"
#include "perimortem/core/scalar.hpp"

namespace Perimortem::Core::Access {

// A borrowed read and write view of bytes with no endianness. Indexed access
// selects the nearest endpoint when the index lies outside a nonempty view.
// Empty storage has no endpoint, so indexed access returns None. get_data
// exposes raw storage for callers that can prove their own bounds.
class Bytes {
 public:
  using data_type = U8;

  // Default to empty string.
  constexpr Bytes() = default;

  constexpr Bytes(const Bytes&) = default;

  constexpr Bytes(data_type* source, Count source_size)
      : source_block(source), size(source ? source_size : 0) {}

  template <Count N>
  constexpr Bytes(data_type (&source)[N]) : source_block(source), size(N) {}

  constexpr operator View::Bytes() const { return get_view(); }

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
      -> Access::Bytes {
    if (start >= get_size()) {
      return Access::Bytes();
    }

    return Access::Bytes(
        source_block + start, Core::Scalar::min(size, get_size() - start));
  };

  constexpr auto is_empty() const -> Bool { return size == 0; };
  constexpr auto get_size() const -> Count { return size; };
  constexpr auto get_data() -> data_type* { return source_block; };
  constexpr auto get_data() const -> const data_type* { return source_block; };
  constexpr auto get_view() const -> const View::Bytes {
    return View::Bytes(source_block, get_size());
  }

 private:
  data_type* source_block = nullptr;
  Count size = 0;
};

}  // namespace Perimortem::Core::Access
