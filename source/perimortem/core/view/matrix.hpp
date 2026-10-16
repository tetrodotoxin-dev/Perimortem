// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/math/point.hpp"
#include "perimortem/core/math/size.hpp"
#include "perimortem/core/storage/option.hpp"
#include "perimortem/core/view/vector.hpp"

namespace Perimortem::Core::View {

// Matrix borrows a contiguous range of read only values and assigns one value
// to every coordinate in its multidimensional Size. Axis zero is contiguous
// and each following axis advances by the product of the preceding extents.
// Construction publishes the supplied Size only when its value count is
// representable and exactly matches the supplied View::Vector. Every other
// input produces the null representation with zero extents and no values. A
// valid Size with an empty axis retains its other extents while referencing no
// values.
//
// `get` rejects negative coordinates and coordinates outside any axis. The
// caller keeps every value alive at the same address through all access.
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
      if (expected != source.get_size() ||
          (expected != 0 && source.get_data() == nullptr)) {
        return;
      }

      size = source_size;
      values = source;
    };

    auto reject = []() {};
    source_size.get_element_count().visit(publish, reject);
  }

  constexpr auto get(Math::Point<S64, dimensions> point) const
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

    return values.get_data()[index];
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
  Math::Size<U32, dimensions> size;
  View::Vector<data_type> values;
};

}  // namespace Perimortem::Core::View
