// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/access/vector.hpp"
#include "perimortem/core/math/point.hpp"
#include "perimortem/core/math/size.hpp"
#include "perimortem/core/storage/option.hpp"
#include "perimortem/core/view/matrix.hpp"

namespace Perimortem::Core::Access {

// Matrix borrows a contiguous range of writable values and assigns one value
// to every coordinate in its multidimensional Size. Axis zero is contiguous
// and each following axis advances by the product of the preceding extents.
// Construction publishes the supplied Size only when its value count is
// representable and exactly matches the supplied Access::Vector. Every other
// input produces the null representation with zero extents and no values. A
// valid Size with an empty axis retains its other extents while referencing no
// values.
//
// `get` rejects negative coordinates and coordinates outside any axis. A
// successful lookup returns a writable reference. The caller keeps every value
// alive at the same address and synchronizes concurrent access.
template <typename type, Count dimensions>
class Matrix {
  static_assert(dimensions > 0);

 public:
  using data_type = type;

  constexpr Matrix() = default;
  constexpr Matrix(
      Math::Size<U32, dimensions> source_size,
      Access::Vector<data_type> source) {
    data_type* source_data = source.get_data();
    auto publish = [this, source_size, source, source_data](Count expected) {
      if (expected != source.get_size() ||
          (expected != 0 && source_data == nullptr)) {
        return;
      }

      size = source_size;
      values = source;
    };

    auto reject = []() {};
    source_size.get_element_count().visit(publish, reject);
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
  Math::Size<U32, dimensions> size;
  Access::Vector<data_type> values;
};

}  // namespace Perimortem::Core::Access
