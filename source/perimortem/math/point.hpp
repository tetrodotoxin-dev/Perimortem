// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/static/vector.hpp"

namespace Perimortem::Math {

// Point gives coordinates a spatial role without choosing a unit or a fixed
// number of axes. It aggregates Static::Vector because coordinates have no
// validity restriction and need no second storage policy or constructor.
template <typename type, Count dimensions>
class Point {
  static_assert(dimensions > 0);

 public:
  Core::Static::Vector<type, dimensions> coordinates;

  constexpr auto operator[](Count axis) const -> type {
    return coordinates[axis];
  }

  constexpr auto operator[](Count axis) -> type& {
    return coordinates[axis];
  }

};

static_assert(sizeof(Point<S32, 2>) == sizeof(S32) * 2);
static_assert(__is_standard_layout(Point<S32, 2>));
static_assert(__is_aggregate(Point<S32, 2>));

}  // namespace Perimortem::Math
