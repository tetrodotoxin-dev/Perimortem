// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/access/matrix.hpp"
#include "perimortem/core/math/domain.hpp"
#include "perimortem/core/storage/option.hpp"
#include "perimortem/core/storage/result.hpp"
#include "perimortem/core/view/matrix.hpp"
#include "perimortem/memory/thread/vector.hpp"

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
// values. `get_view()` and `get_access()` expose the shape together with its
// row major storage but do not carry the Domain's boundary policies. They
// remain valid until Matrix storage changes. Use Matrix `get` when access needs
// those policies.
//
// Its Boundary arguments follow Domain directly. No arguments select Clip, one
// selects the same rule for both axes, and two select column then row behavior.
template <typename type, Core::Math::Boundary... boundaries>
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
      Core::Math::Domain<2, boundaries...> source_domain,
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
      Core::Math::Domain<2, boundaries...> source_domain,
      Thread::Vector<type>&& source) -> Core::Storage::Result<Matrix, Failure> {
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

  constexpr auto get_domain() const -> Core::Math::Domain<2, boundaries...> {
    return domain;
  }

  constexpr auto get_column_count() const -> U32 {
    return domain.get_extent(0);
  }

  constexpr auto get_row_count() const -> U32 { return domain.get_extent(1); }

  constexpr operator Core::View::Matrix<type, 2>() const { return get_view(); }

  constexpr auto get_view() const -> Core::View::Matrix<type, 2> {
    return Core::View::Matrix<type, 2>(domain.get_size(), values.get_view());
  }

  constexpr auto get_access() -> Core::Access::Matrix<type, 2> {
    auto return_access = [](Core::Access::Matrix<type, 2>& access) {
      return access;
    };

    auto unreachable = []() -> Core::Access::Matrix<type, 2> {
      __builtin_unreachable();
    };

    return Core::Access::Matrix<type, 2>::create(
               domain.get_size(), values.get_access())
        .visit(return_access, unreachable);
  }

  constexpr auto is_empty() const -> Bool { return domain.is_empty(); }

  // Clip can reject a coordinate, and no policy can select a value from an
  // empty axis. A successful lookup returns a reference that remains valid
  // until Matrix storage changes.
  constexpr auto get(Core::Math::Point<S64, 2> point) const
      -> Core::Storage::Option<const type&> {
    return domain.resolve(point).visit(
        [this](Core::Math::Point<U32, 2> mapped)
            -> Core::Storage::Option<const type&> {
          return values[get_index(mapped)];
        },

        []() -> Core::Storage::Option<const type&> { return {}; });
  }

  constexpr auto get(Core::Math::Point<S64, 2> point)
      -> Core::Storage::Option<type&> {
    return domain.resolve(point).visit(
        [this](
            Core::Math::Point<U32, 2> mapped) -> Core::Storage::Option<type&> {
          return values[get_index(mapped)];
        },

        []() -> Core::Storage::Option<type&> { return {}; });
  }

 private:
  constexpr auto get_index(Core::Math::Point<U32, 2> point) const -> Count {
    Count index = point[1];
    index *= get_column_count();
    index += point[0];
    return index;
  }

  Thread::Vector<type> values;
  Core::Math::Domain<2, boundaries...> domain;
};

}  // namespace Perimortem::Memory
