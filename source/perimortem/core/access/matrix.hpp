// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/access/vector.hpp"
#include "perimortem/core/math/point.hpp"
#include "perimortem/core/math/size.hpp"
#include "perimortem/core/storage/option.hpp"
#include "perimortem/core/view/matrix.hpp"

namespace Perimortem::Core::Access {

// Matrix provides allocation free write access to one value for every
// coordinate in its multidimensional Size. Axis zero is contiguous and each
// following axis advances by the product of the preceding extents. Its value
// count is always representable and exactly matches the supplied
// Access::Vector. A valid Size with an empty axis retains its other extents
// while referencing no values.
//
// Coordinate access returns None outside any axis. Read only projection keeps
// the same geometry. Matrix does not extend the source storage lifetime.
// Relocating or releasing the referenced storage invalidates every Access and
// View into it. Callers synchronize concurrent writes.
template <typename type, Count dimensions>
class Matrix {
  static_assert(dimensions > 0);

 public:
  using data_type = type;

  Matrix() = delete;

  // Returns None when the Size overflows Count or its value count differs from
  // the supplied range. Every selected Matrix can therefore resolve a valid
  // coordinate to exactly one writable value.
  static constexpr auto create(
      Math::Size<U32, dimensions> source_size,
      Access::Vector<data_type> source) -> Storage::Option<Matrix> {
    auto publish = [source_size,
                    source](Count expected) -> Storage::Option<Matrix> {
      if (expected != source.get_size()) {
        return {};
      }

      return Matrix(source_size, source);
    };

    auto reject = []() -> Storage::Option<Matrix> { return {}; };
    return source_size.get_element_count().visit(publish, reject);
  }

  constexpr auto get(Math::Point<S64, dimensions> point)
      -> Storage::Option<data_type&> {
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

    return values.get_data()[index];
  }

  constexpr operator View::Matrix<data_type, dimensions>() const {
    return get_view();
  }

  constexpr auto get_size() const -> Math::Size<U32, dimensions> {
    return size;
  }

  constexpr auto get_extent(Count axis) const -> U32 { return size[axis]; }
  constexpr auto get_value_count() const -> Count { return values.get_size(); }
  constexpr auto get_values() -> Access::Vector<data_type> { return values; }
  constexpr auto get_view() const -> View::Matrix<data_type, dimensions> {
    return View::Matrix<data_type, dimensions>(size, values.get_view());
  }

  constexpr auto is_empty() const -> Bool { return size.is_empty(); }

 private:
  constexpr Matrix(
      Math::Size<U32, dimensions> source_size,
      Access::Vector<data_type> source)
      : size(source_size), values(source) {}

  Math::Size<U32, dimensions> size;
  Access::Vector<data_type> values;
};

}  // namespace Perimortem::Core::Access
