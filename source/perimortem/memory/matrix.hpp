// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/view/vector.hpp"
#include "perimortem/core/option.hpp"

#include "perimortem/memory/dynamic/vector.hpp"

#include "perimortem/utility/result.hpp"

#include "perimortem/math/domain.hpp"

namespace Perimortem::Memory {

// Matrix represents a bounded two dimensional collection in one contiguous
// allocation. Rows lie end to end, so traversing one reads consecutive values
// without allocating each row separately. A flat index cannot distinguish a
// column past the right edge from the next row. Domain resolves the column and
// row independently before Matrix calculates the flat index, so a boundary
// rule acts on the intended axis rather than on the entire allocation.
//
// A Matrix has exactly row_count * column_count live values. An empty
// axis leaves no addressable values. Flat views expose storage order directly
// and borrow their values from the Matrix.
//
// Clip is the default boundary behavior on both axes, but you can explicitly
// provide different boundary behavior for row and columns separately.
template <
    typename type,
    Math::Boundary column_boundary = Math::Boundary::Clip,
    Math::Boundary row_boundary = column_boundary>
class Matrix {
 public:
  enum class Failure : U8 {
    BadDimensions,
  };

  constexpr Matrix() = default;
  constexpr Matrix(const Matrix&) = default;
  constexpr Matrix(Matrix&& source) {
    Core::Data::swap(values, source.values);
    Core::Data::swap(domain, source.domain);
  }

  constexpr auto operator=(const Matrix&) -> Matrix& = default;
  constexpr auto operator=(Matrix&& source) -> Matrix& {
    if (this != &source) {
      Core::Data::swap(values, source.values);
      Core::Data::swap(domain, source.domain);
    }

    return *this;
  }

  // Creation copies from a borrowed flat view. Values beyond the Domain's
  // extent are ignored, and missing positions receive the type's default
  // value. The published Matrix has one value for every coordinate.
  static auto create(
      Math::Domain<2, column_boundary, row_boundary> source_domain,
      Core::View::Vector<type> source) -> Matrix
    requires(__is_constructible(type, const type&) && __is_constructible(type))
  {
    Count count = source_domain.get_extent(0);
    count *= source_domain.get_extent(1);
    Dynamic::Vector<type> copied(count);
    for (Count index = 0; index < count && index < source.get_size(); index++) {
      copied.insert(source.get_data()[index]);
    }

    if (copied.get_size() < count) {
      const type padding{};
      while (copied.get_size() < count) {
        copied.insert(padding);
      }
    }

    Matrix matrix;
    Core::Data::swap(matrix.values, copied);
    matrix.domain = source_domain;
    return matrix;
  }

  // Projection transfers an owned flat vector without copying its values.
  // Its length must equal the Domain's area, so every coordinate has one
  // existing value. A mismatch leaves the supplied vector with its values.
  static auto project(
      Math::Domain<2, column_boundary, row_boundary> source_domain,
      Dynamic::Vector<type>&& source) -> Utility::Result<Matrix, Failure> {
    Count count = source_domain.get_extent(0);
    count *= source_domain.get_extent(1);
    if (source.get_size() != count) {
      return Failure::BadDimensions;
    }

    Matrix matrix;
    Core::Data::swap(matrix.values, source);
    matrix.domain = source_domain;
    return matrix;
  }

  constexpr auto get_domain() const
      -> Math::Domain<2, column_boundary, row_boundary> {
    return domain;
  }

  constexpr auto get_column_count() const -> U32 {
    return domain.get_extent(0);
  }
  constexpr auto get_row_count() const -> U32 { return domain.get_extent(1); }

  constexpr auto get_values() const -> Core::View::Vector<type> {
    return values.get_view();
  }

  constexpr auto get_access() -> Core::Access::Vector<type> {
    return values.get_access();
  }

  constexpr auto is_empty() const -> Bool { return domain.is_empty(); }

  // Clip can reject a coordinate, and no policy can select a value from an
  // empty axis. A successful lookup borrows its value until storage changes.
  constexpr auto get(Math::Point<S64, 2> point) const
      -> Core::Option<const type&> {
    auto mapped = domain.resolve(point);
    if (!mapped) {
      return {};
    }

    return values[get_index(*mapped)];
  }

  constexpr auto get(Math::Point<S64, 2> point) -> Core::Option<type&> {
    auto mapped = domain.resolve(point);
    if (!mapped) {
      return {};
    }

    return values[get_index(*mapped)];
  }

 private:
  constexpr auto get_index(Math::Point<U32, 2> point) const -> Count {
    Count index = point[1];
    index *= get_column_count();
    index += point[0];
    return index;
  }

  Dynamic::Vector<type> values;
  Math::Domain<2, column_boundary, row_boundary> domain;
};

}  // namespace Perimortem::Memory
