// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/option.hpp"

namespace Perimortem::Utility {

// A half open interval stores its first index and element count. Its exclusive
// end must fit in `Count`, so its greatest value cannot be included in the
// interval.
struct Range {
  Count start = 0;
  Count size = 0;

  constexpr auto get_end() const -> Core::Option<Count> {
    if (size > Count(-1) - start) {
      return Core::Option<Count>();
    }

    return start + size;
  }

  constexpr auto is_empty() const -> Bool { return size == 0; }
  constexpr auto is_valid() const -> Bool { return Bool(get_end()); }
  constexpr auto has_overlap(Range other) const -> Bool {
    if (is_empty() || other.is_empty()) {
      return False;
    }

    return get_end().visit(
        [&](Count end) {
          return other.get_end().visit(
              [&](Count other_end) {
                return Bool(start < other_end && other.start < end);
              },

              []() { return False; });
        },

        []() { return False; });
  }

  // Expands the interval to include `index` while preserving every index it
  // already contains. An empty interval begins at `index`. `Count(-1)` cannot
  // be included because its exclusive end is not representable. False leaves
  // both bounds unchanged when the current interval or requested index is
  // invalid.
  constexpr auto include(Count index) -> Bool {
    return get_end().visit(
        [&](Count end) {
          if (index == Count(-1)) {
            return False;
          }

          if (is_empty()) {
            start = index;
            size = 1;
          } else if (index < start) {
            size = end - index;
            start = index;
          } else if (index >= end) {
            size = index - start + 1;
          }

          return True;
        },

        []() { return False; });
  }
};

}  // namespace Perimortem::Utility
