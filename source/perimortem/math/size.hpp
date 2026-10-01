// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/static/vector.hpp"
#include "perimortem/utility/result.hpp"

namespace Perimortem::Math {

// Size keeps every axis finite and nonnegative. The ordinary constructor
// clips invalid components to zero so an existing Size is always usable.
// create reports that normalization instead when losing an input matters.
// Storage stays private because write access to a component would break the
// promise immediately after construction.
template <typename type, Count dimensions>
class Size {
  static_assert(dimensions > 0);
  static_assert(
      (__is_integral(type) && !__is_same(type, bool)) ||
      __is_same(type, R32) || __is_same(type, R64));

 public:
  enum class Failure : U8 {
    Clipped,
    NonFinite,
  };

  constexpr Size() = default;
  constexpr Size(Core::Static::Vector<type, dimensions> source) {
    for (Count axis = 0; axis < dimensions; axis++) {
      components[axis] = normalize(source[axis]);
    }
  }

  // Result selects either the exact size or the reason construction would
  // replace input. A caller wanting the clipped value uses Size directly.
  static constexpr auto create(Core::Static::Vector<type, dimensions> source)
      -> Utility::Result<Size, Failure> {
    for (Count axis = 0; axis < dimensions; axis++) {
      if (!finite(source[axis])) {
        return Failure::NonFinite;
      }
    }

    for (Count axis = 0; axis < dimensions; axis++) {
      if (source[axis] < type{}) {
        return Failure::Clipped;
      }
    }

    return Size(source);
  }

  constexpr auto operator[](Count axis) const -> type {
    return components[axis];
  }

  constexpr auto is_empty() const -> Bool {
    for (Count axis = 0; axis < dimensions; axis++) {
      if (components[axis] == type{}) {
        return True;
      }
    }

    return False;
  }

 private:
  static constexpr auto finite(type value) -> Bool {
    if constexpr (__is_same(type, R32) || __is_same(type, R64)) {
      return __builtin_isfinite(value);
    }

    return True;
  }

  static constexpr auto normalize(type value) -> type {
    if (!finite(value) || value < type{}) {
      return type{};
    }

    return value;
  }

  Core::Static::Vector<type, dimensions> components;
};

static_assert(sizeof(Size<U32, 2>) == sizeof(U32) * 2);
static_assert(__is_standard_layout(Size<U32, 2>));

}  // namespace Perimortem::Math
