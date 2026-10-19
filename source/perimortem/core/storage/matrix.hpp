// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/access/matrix.hpp"
#include "perimortem/core/math/point.hpp"
#include "perimortem/core/math/size.hpp"
#include "perimortem/core/storage/option.hpp"
#include "perimortem/core/storage/vector.hpp"
#include "perimortem/core/view/matrix.hpp"

namespace Perimortem::Core::Storage {

// Matrix embeds one value for every coordinate in its compile time Size. Axis
// zero is contiguous and each following axis advances by the product of the
// preceding extents. The shape and value addresses never change, so views and
// writable access remain valid for the Matrix lifetime without an allocation.
//
// Template extents are ordered by axis. Every extent must be greater than zero
// and their product must fit in Count. Default construction value initializes
// every position. Aggregate construction lists values with axis zero changing
// first. Indexing returns None when a coordinate is negative or outside its
// axis.
template <typename type, U32... extents>
class Matrix {
  static constexpr Count dimension_count = sizeof...(extents);
  static_assert(dimension_count > 0);

  static constexpr Storage::Vector<U32, dimension_count> shape = {{extents...}};

  static constexpr auto calculate_value_count() -> Count {
    Count count = 1;
    for (Count axis = 0; axis < dimension_count; axis++) {
      Count product;
      if (__builtin_mul_overflow(count, Count(shape[axis]), &product)) {
        return Count();
      }

      count = product;
    }

    return count;
  }

  static constexpr Count storage_count = calculate_value_count();
  static_assert(storage_count > 0);

  struct Values {
    type source_block[storage_count];
  };

  Values values{};

 public:
  using data_type = type;

  static constexpr Count dimensions = dimension_count;
  static constexpr Math::Size<U32, dimensions> size =
      Math::Size<U32, dimensions>(shape);
  static constexpr Count value_count = storage_count;

  constexpr Matrix() {}

  // Aggregate construction initializes the complete storage from one source.
  // Call sites use an extra pair of braces, as in
  // `Storage::Matrix<U8, 2, 2> values = {{1, 2, 3, 4}}`.
  constexpr Matrix(const Values& source) : values(source) {}

  constexpr operator View::Matrix<data_type, dimensions>() const {
    return get_view();
  }

  constexpr operator Access::Matrix<data_type, dimensions>() {
    return get_access();
  }

  constexpr auto operator[](Math::Point<S64, dimensions> point)
      -> Storage::Option<data_type&> {
    return get_access()[point];
  }

  constexpr auto operator[](Math::Point<S64, dimensions> point) const
      -> Storage::Option<const data_type&> {
    Count index = 0;
    Count stride = 1;
    for (Count axis = 0; axis < dimensions; axis++) {
      const S64 coordinate = point[axis];
      if (coordinate < 0 || coordinate >= size[axis]) {
        return {};
      }

      index += Count(coordinate) * stride;
      stride *= size[axis];
    }

    return values.source_block[index];
  }

  constexpr auto get_size() const -> Math::Size<U32, dimensions> {
    return size;
  }

  constexpr auto get_extent(Count axis) const -> U32 { return size[axis]; }
  constexpr auto get_value_count() const -> Count { return value_count; }
  constexpr auto get_values() const -> View::Vector<data_type> {
    return View::Vector<data_type>(values.source_block[0], value_count);
  }

  constexpr auto get_values() -> Access::Vector<data_type> {
    return Access::Vector<data_type>(values.source_block[0], value_count);
  }

  constexpr auto get_view() const -> View::Matrix<data_type, dimensions> {
    return View::Matrix<data_type, dimensions>(size, get_values());
  }

  constexpr auto get_access() -> Access::Matrix<data_type, dimensions> {
    auto return_access = [](Access::Matrix<data_type, dimensions>& access) {
      return access;
    };

    auto unreachable = []() -> Access::Matrix<data_type, dimensions> {
      __builtin_unreachable();
    };

    return Access::Matrix<data_type, dimensions>::create(size, get_values())
        .visit(return_access, unreachable);
  }

  constexpr auto is_empty() const -> Bool { return False; }
};

}  // namespace Perimortem::Core::Storage
