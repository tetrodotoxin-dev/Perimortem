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
// first.
template <typename type, U32... extents>
class Matrix {
 public:
  using data_type = type;

  static constexpr Count dimensions = sizeof...(extents);
  static_assert(dimensions > 0);

 private:
  static constexpr Storage::Vector<U32, dimensions> shape = {{extents...}};

 public:
  static constexpr Math::Size<U32, dimensions> size =
      Math::Size<U32, dimensions>(shape);
  static constexpr Count value_count = size.get_element_count().visit(
      [](Count count) { return count; },
      []() { return Count(); });
  static_assert(value_count > 0);

 private:
  struct Values {
    data_type source_block[value_count];
  };

  Values values{};

 public:
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

  constexpr auto get(Math::Point<S64, dimensions> point)
      -> Storage::Option<data_type&> {
    return get_access().get(point);
  }

  constexpr auto get(Math::Point<S64, dimensions> point) const
      -> Storage::Option<const data_type&> {
    return get_view().get(point);
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
