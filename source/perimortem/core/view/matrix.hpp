// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/math/point.hpp"
#include "perimortem/core/math/size.hpp"
#include "perimortem/core/storage/option.hpp"
#include "perimortem/core/view/vector.hpp"

namespace Perimortem::Core::View {

// Matrix provides allocation free read access to one value for every coordinate
// in its multidimensional Size. Axis zero is contiguous and each following axis
// advances by the product of the preceding extents. Construction preserves the
// supplied Size only when its value count is representable and exactly matches
// the supplied View::Vector. Every other input produces the zero initialized
// representation. A valid Size with an empty axis retains its other extents
// while referencing no values.
//
// Indexing never reads outside the supplied range with negative coordinates or
// coordinates outside any axis returning a zero initialized value. Matrix does
// not extend the source storage lifetime. Relocating or releasing the source
// values invalidates Matrix, so callers reacquire it after such an operation.
template <typename type, Count dimensions>
class Matrix {
  static_assert(dimensions > 0);

 public:
  using data_type = type;

  constexpr Matrix() = default;
  constexpr Matrix(
      Math::Size<U32, dimensions> source_size,
      View::Vector<data_type> source) {
    auto publish = [this, source_size, source](Count expected) {
      if (expected != source.get_size()) {
        return;
      }

      size = source_size;
      values = source;
    };

    auto reject = []() {};
    calculate_value_count(source_size).visit(publish, reject);
  }

  constexpr auto operator[](Math::Point<S64, dimensions> point) const
      -> data_type {
    Count index = 0;
    Count stride = 1;
    for (Count axis = 0; axis < dimensions; axis++) {
      const S64 coordinate = point[axis];
      if (coordinate < 0 || coordinate >= size[axis]) {
        return data_type();
      }

      index += Count(coordinate) * stride;
      stride *= size[axis];
    }

    return values[index];
  }

  constexpr auto get_size() const -> Math::Size<U32, dimensions> {
    return size;
  }

  constexpr auto get_extent(Count axis) const -> U32 { return size[axis]; }
  constexpr auto get_value_count() const -> Count { return values.get_size(); }
  constexpr auto get_values() const -> View::Vector<data_type> {
    return values;
  }

  constexpr auto is_empty() const -> Bool { return size.is_empty(); }

 private:
  static constexpr auto calculate_value_count(
      Math::Size<U32, dimensions> source_size) -> Storage::Option<Count> {
    Count count = 1;
    for (Count axis = 0; axis < dimensions; axis++) {
      Count product;
      if (__builtin_mul_overflow(count, Count(source_size[axis]), &product)) {
        return {};
      }

      count = product;
    }

    return count;
  }

  Math::Size<U32, dimensions> size;
  View::Vector<data_type> values;
};

}  // namespace Perimortem::Core::View
