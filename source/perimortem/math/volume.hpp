// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/math/point.hpp"
#include "perimortem/math/size.hpp"

namespace Perimortem::Math {

// Volume bounds an axis aligned region in Point's coordinate space. The
// origin is included and every upper edge is excluded, so adjacent regions
// can share an edge without sharing points. Size normalizes invalid extents
// before Volume observes them. Periodic axes such as polar angle need their
// own range policy for intervals crossing a wrap seam.
template <typename type, Count dimensions>
class Volume {
  static_assert(dimensions > 0);
  static_assert(sizeof(type) <= sizeof(U64));

 public:
  constexpr Volume() = default;
  constexpr Volume(
      Point<type, dimensions> origin, Size<type, dimensions> size)
      : origin(origin), size(size) {}

  constexpr auto get_origin() const -> Point<type, dimensions> {
    return origin;
  }
  constexpr auto get_size() const -> Size<type, dimensions> { return size; }
  constexpr auto is_empty() const -> Bool { return size.is_empty(); }

  // Reject coordinates before the origin first. Subtraction in an unsigned
  // width then recovers the distance even when signed endpoints span their
  // entire range. Floating comparisons reject NaN coordinates and keep the
  // scalar's ordinary precision.
  constexpr auto contains(Point<type, dimensions> point) const -> Bool {
    for (Count axis = 0; axis < dimensions; axis++) {
      if constexpr (__is_same(type, R32) || __is_same(type, R64)) {
        if (!(point[axis] >= origin[axis])) {
          return False;
        }

        type distance = point[axis] - origin[axis];
        if (!(distance < size[axis])) {
          return False;
        }
      } else if constexpr (sizeof(type) <= sizeof(U32)) {
        if (point[axis] < origin[axis]) {
          return False;
        }

        U32 distance = point[axis];
        distance -= origin[axis];
        if (distance >= size[axis]) {
          return False;
        }
      } else {
        if (point[axis] < origin[axis]) {
          return False;
        }

        U64 distance = point[axis];
        distance -= origin[axis];
        if (distance >= size[axis]) {
          return False;
        }
      }
    }

    return True;
  }

 private:
  Point<type, dimensions> origin;
  Size<type, dimensions> size;
};

}  // namespace Perimortem::Math
