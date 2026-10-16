// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/option.hpp"
#include "perimortem/core/view/vector.hpp"
#include "perimortem/math/domain.hpp"
#include "perimortem/memory/thread/vector.hpp"
#include "perimortem/utility/result.hpp"

namespace Perimortem::Memory {

// Matrix stores a two dimensional Domain in one contiguous row major
// allocation. Adjacent columns occupy adjacent values without a separate
// allocation for each row. A flat index cannot distinguish a column past the
// right edge from the next row. Domain therefore resolves the column and row
// independently before Matrix calculates that index, keeping each boundary
// rule on its declared axis.
//
// Each axis extent is U32, so the product of the two extents fits in Count. A
// Matrix has exactly that many live values. An empty axis leaves no addressable
// values. `get_values()` exposes row major storage through a View that remains
// valid until Matrix storage changes.
//
// Clip is the default for both axes. The template can select different column
// and row boundary rules when their coordinates require different behavior.
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

  // Creation copies from the source View. Values beyond the Domain's extent
  // are ignored, and missing positions receive the type's default value. The
  // resulting Matrix has one value for every coordinate.
  static auto create(
      Math::Domain<2, column_boundary, row_boundary> source_domain,
      Core::View::Vector<type> source) -> Matrix
    requires(__is_constructible(type, const type&) && __is_constructible(type))
  {
    Count count = source_domain.get_extent(0);
    count *= source_domain.get_extent(1);

    Thread::Vector<type> copied(count);
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

  // Projection consumes a flat Thread Vector by moving its allocation without
  // copying values.
  // Its length must equal the Domain's area, so every coordinate has one
  // existing value. A mismatch leaves the supplied vector with its values.
  static auto project(
      Math::Domain<2, column_boundary, row_boundary> source_domain,
      Thread::Vector<type>&& source) -> Utility::Result<Matrix, Failure> {
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
  // empty axis. A successful lookup returns a reference that remains valid
  // until Matrix storage changes.
  constexpr auto get(Math::Point<S64, 2> point) const
      -> Core::Option<const type&> {
    return domain.resolve(point).visit(
        [this](Math::Point<U32, 2> mapped) -> Core::Option<const type&> {
          return values[get_index(mapped)];
        },

        []() -> Core::Option<const type&> { return {}; });
  }

  constexpr auto get(Math::Point<S64, 2> point) -> Core::Option<type&> {
    return domain.resolve(point).visit(
        [this](Math::Point<U32, 2> mapped) -> Core::Option<type&> {
          return values[get_index(mapped)];
        },

        []() -> Core::Option<type&> { return {}; });
  }

 private:
  constexpr auto get_index(Math::Point<U32, 2> point) const -> Count {
    Count index = point[1];
    index *= get_column_count();
    index += point[0];
    return index;
  }

  Thread::Vector<type> values;
  Math::Domain<2, column_boundary, row_boundary> domain;
};

}  // namespace Perimortem::Memory
