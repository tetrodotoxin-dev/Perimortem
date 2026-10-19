// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

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

  // Volume is expressed in the same scalar type as each extent, preserving the
  // Size unit instead of assigning a storage interpretation. An empty Size or
  // a product that cannot be represented by `type` returns zero.
  constexpr auto get_volume() const -> type {
    type volume = type(1);
    for (Count axis = 0; axis < dimensions; axis++) {
      type product;
      if constexpr (__is_integral(type)) {
        if (__builtin_mul_overflow(volume, components[axis], &product)) {
          return type();
        }
      } else {
        product = volume * components[axis];
        if (!finite(product)) {
          return type();
        }
      }

      volume = product;
    }

    return volume;
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
