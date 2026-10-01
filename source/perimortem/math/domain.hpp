// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/option.hpp"
#include "perimortem/core/static/vector.hpp"
#include "perimortem/math/boundary.hpp"
#include "perimortem/math/point.hpp"
#include "perimortem/core/scalar.hpp"
#include "perimortem/math/size.hpp"
#include "perimortem/math/volume.hpp"

namespace Perimortem::Math {

// Domain maps signed coordinates into discrete index limits starting at zero.
// Size owns each upper limit while Boundary selects the behavior outside it.
//
// Domain supports three modes based on its Boundary template arguments:
//
// - If none are passed, Clip is used for all dimensions.
// - If one is passed, it is used for all dimensions.
// - Otherwise, one Boundary is required for each dimension.
//
// The selected rules are part of the type, so resolution needs no runtime
// policy dispatch. An empty axis has no valid index under any policy.
// Domain owns no elements and chooses no storage order.
template <Count dimensions, Boundary... boundaries>
class Domain {
  static_assert(
      sizeof...(boundaries) == 0 || sizeof...(boundaries) == 1 ||
      sizeof...(boundaries) == dimensions);
  static_assert((
      (boundaries == Boundary::Clip ||
       boundaries == Boundary::Saturate ||
       boundaries == Boundary::Mirror ||
       boundaries == Boundary::Wrap) && ...));

 public:
  constexpr Domain() = default;
  constexpr Domain(Size<U32, dimensions> source) : extent(source) {}

  constexpr auto get_size() const -> Size<U32, dimensions> { return extent; }
  constexpr auto get_extent(Count axis) const -> U32 { return extent[axis]; }
  constexpr auto get_boundary(Count axis) const -> Boundary {
    if constexpr (sizeof...(boundaries) == 0) {
      return Boundary::Clip;
    } else {
      constexpr Core::Static::Vector<Boundary, sizeof...(boundaries)> selected =
          {{boundaries...}};
      if constexpr (sizeof...(boundaries) == 1) {
        return selected[0];
      } else {
        return selected[axis];
      }
    }
  }
  constexpr auto is_empty() const -> Bool { return extent.is_empty(); }

  constexpr auto get_limits() const -> Volume<S64, dimensions> {
    Core::Static::Vector<S64, dimensions> limits;
    for (Count axis = 0; axis < dimensions; axis++) {
      limits[axis] = extent[axis];
    }

    return Volume<S64, dimensions>(
        Point<S64, dimensions>(), Size<S64, dimensions>(limits));
  }

  constexpr auto contains(Point<U32, dimensions> point) const -> Bool {
    for (Count axis = 0; axis < dimensions; axis++) {
      if (point[axis] >= extent[axis]) {
        return False;
      }
    }

    return True;
  }

  constexpr auto contains(Point<S64, dimensions> point) const -> Bool {
    for (Count axis = 0; axis < dimensions; axis++) {
      if (point[axis] < 0 || point[axis] >= extent[axis]) {
        return False;
      }
    }

    return True;
  }

  // Resolve returns a valid index or None. Clip rejects coordinates outside
  // its axis, while the other policies map them into the finite limits. No
  // policy can produce an index when any axis is empty.
  constexpr auto resolve(Point<S64, dimensions> point) const
      -> Core::Option<Point<U32, dimensions>> {
    if (is_empty()) {
      return {};
    }

    Core::Static::Vector<U32, dimensions> mapped;
    if (!resolve_axis<0>(point, mapped)) {
      return {};
    }

    return Point<U32, dimensions>{{mapped}};
  }

  // Every axis is at most 32 bits, but several axes can still overflow Count.
  // Returning None lets a storage owner refuse the allocation before fitting
  // its buffer.
  constexpr auto get_element_count() const -> Core::Option<Count> {
    if (is_empty()) {
      return Count{};
    }

    Count count = 1;
    for (Count axis = 0; axis < dimensions; axis++) {
      if (count > ~Count{} / extent[axis]) {
        return Core::Option<Count>();
      }

      count *= extent[axis];
    }

    return count;
  }

 private:
  template <Count axis>
  static consteval auto boundary_at() -> Boundary {
    if constexpr (sizeof...(boundaries) == 0) {
      return Boundary::Clip;
    } else {
      constexpr Core::Static::Vector<Boundary, sizeof...(boundaries)> selected =
          {{boundaries...}};
      if constexpr (sizeof...(boundaries) == 1) {
        return selected[0];
      } else {
        return selected[axis];
      }
    }
  }

  // The policy is fixed for each axis, so resolving a coordinate needs no
  // stored policy value or runtime boundary dispatch.
  template <Count axis>
  constexpr auto resolve_axis(
      const Point<S64, dimensions>& point,
      Core::Static::Vector<U32, dimensions>& mapped) const -> Bool {
    if constexpr (axis == dimensions) {
      return True;
    } else {
      const S64 limit = extent[axis];
      const S64 coordinate = point[axis];
      if (limit == 0) {
        return False;
      }

      S64 selected = 0;
      if constexpr (boundary_at<axis>() == Boundary::Clip) {
        if (coordinate < 0 || coordinate >= limit) {
          return False;
        }

        selected = coordinate;
      } else if constexpr (boundary_at<axis>() == Boundary::Saturate) {
        selected = Core::Scalar::clamp(coordinate, S64{}, limit - 1);
      } else if constexpr (boundary_at<axis>() == Boundary::Mirror) {
        const S64 period = limit * 2;
        const S64 phase = Core::Scalar::wrap(coordinate, period);
        selected = phase < limit ? phase : period - 1 - phase;
      } else {
        selected = Core::Scalar::wrap(coordinate, limit);
      }

      mapped[axis] = selected;
      return resolve_axis<axis + 1>(point, mapped);
    }
  }

  Size<U32, dimensions> extent;
};

}  // namespace Perimortem::Math
