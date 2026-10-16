// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/storage/option.hpp"
#include "perimortem/core/storage/vector.hpp"

namespace Perimortem::Core::Math {

// Size stores one nonnegative finite extent for each axis. Construction
// preserves finite values greater than zero and zero. Negative values, NaN and
// infinities become zero. A Size is empty when any axis is zero. Storage stays
// private because write access to an extent would break these guarantees.
template <typename type, Count dimensions>
class Size {
  static_assert(dimensions > 0);
  static_assert(
      (__is_integral(type) && !__is_same(type, bool)) || __is_same(type, R32) ||
      __is_same(type, R64));

 public:
  constexpr Size() = default;
  constexpr Size(Core::Storage::Vector<type, dimensions> source) {
    for (Count axis = 0; axis < dimensions; axis++) {
      components[axis] = normalize(source[axis]);
    }
  }

  constexpr auto operator[](Count axis) const -> type {
    return components[axis];
  }

  constexpr auto is_empty() const -> Bool {
    for (Count axis = 0; axis < dimensions; axis++) {
      if (components[axis] == type()) {
        return True;
      }
    }

    return False;
  }

  // Integral extents describe a count only when their product fits in Count.
  // An empty axis has zero values, while overflow has no representable count.
  constexpr auto get_element_count() const -> Storage::Option<Count>
    requires(__is_integral(type))
  {
    if (is_empty()) {
      return Count();
    }

    Count count = 1;
    for (Count axis = 0; axis < dimensions; axis++) {
      const Count extent = Count(components[axis]);
      if (count > Count(-1) / extent) {
        return {};
      }

      count *= extent;
    }

    return count;
  }

 private:
  static constexpr auto finite(type value) -> Bool {
    if constexpr (__is_same(type, R32) || __is_same(type, R64)) {
      return __builtin_isfinite(value);
    }

    return True;
  }

  static constexpr auto normalize(type value) -> type {
    if (!finite(value) || value < type()) {
      return type();
    }

    return value;
  }

  Core::Storage::Vector<type, dimensions> components;
};

}  // namespace Perimortem::Core::Math
