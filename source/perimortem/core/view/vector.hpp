// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/math/scalar.hpp"

namespace Perimortem::Core::View {

// Vector provides allocation free read access to one contiguous range of typed
// values. Indexing and slicing never read outside the provided range with out
// of bounds indices returning zero initialized values. `get_data` is the
// explicit escape for code that needs to optimize for speed by providing raw
// pointer access.
//
// Vector does not extend the source storage lifetime. Relocating or releasing
// the referenced storage invalidates every View into it.
//
// An empty View is zero initialized and returns nullptr for `get_data()`.
template <typename type>
class Vector {
 public:
  using data_type = type;

  constexpr Vector() = default;
  constexpr Vector(const Vector&) = default;

  // `first_value` begins one contiguous range containing `value_count` values.
  // The caller keeps that complete range accessible throughout use of the View.
  constexpr Vector(const data_type& first_value, const Count value_count)
      : source_block(value_count == 0 ? nullptr : &first_value),
        size(value_count) {}
  constexpr Vector(type&&, Count) = delete;
  constexpr Vector(const type&&, Count) = delete;

  template <Count N>
  constexpr Vector(const data_type (&source)[N])
      : source_block(source), size(N) {}
  template <Count N>
  constexpr Vector(const data_type (&&)[N]) = delete;

  constexpr auto operator==(const Vector& rhs) const -> Bool {
    if (rhs.size != size) {
      return false;
    }

    for (Count i = 0; i < size; i++) {
      if (source_block[i] != rhs.source_block[i]) {
        return false;
      }
    }

    return true;
  }

  constexpr auto operator[](Count index) const -> data_type {
    if (index >= size) [[unlikely]] {
      return data_type();
    }

    return source_block[index];
  }

  constexpr auto slice(Count start, Count size = Count(-1)) const
      -> View::Vector<data_type> {
    if (start >= get_size()) {
      return View::Vector<data_type>();
    }

    const Count selected_size =
        Core::Math::Scalar::min(size, get_size() - start);
    if (selected_size == 0) {
      return View::Vector<data_type>();
    }

    return View::Vector<data_type>(source_block[start], selected_size);
  };

  constexpr auto is_empty() const -> Bool { return size == 0; };
  constexpr auto get_size() const -> Count { return size; }
  constexpr auto get_data() const -> const data_type* { return source_block; }

 private:
  const type* source_block = nullptr;
  Count size = 0;
};

}  // namespace Perimortem::Core::View
