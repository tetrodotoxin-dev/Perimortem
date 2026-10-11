// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/data.hpp"
#include "perimortem/core/scalar.hpp"
#include "perimortem/core/view/bytes.h"
#include "perimortem/core/view/vector.hpp"

namespace Perimortem::Core::Algorithm {

// Search evaluates positional queries over contiguous storage. Source values
// remain in place while the result identifies the location that satisfies the
// query.
class Search {
 public:
  // Returns zero for an empty value and the all bits set Count sentinel when no
  // complete match exists. AVX2 filters candidate offsets when available before
  // verifying complete substring equality.
  static EXPORTED(PERIMORTEM) auto match(View::Bytes source, View::Bytes value)
      -> Count;

  // Returns the first matching byte offset or the all bits set Count sentinel.
  // AVX2 compares one register of candidates at a time when available.
  static EXPORTED(PERIMORTEM) auto match(View::Bytes source, U8 value) -> Count;

  // Returns the first index containing the minimum value. Strict comparison
  // preserves the earliest index when values tie. An empty source returns zero.
  template <typename element_type>
  static constexpr auto min(View::Vector<element_type> source) -> Count {
    const auto* data = source.get_data();
    Count target_index = 0;
    for (Count index = 1; index < source.get_size(); index++) {
      if (data[index] < data[target_index]) {
        target_index = index;
      }
    }

    return target_index;
  }

  // Returns the first index containing the maximum value. Strict comparison
  // preserves the earliest index when values tie. An empty source returns zero.
  template <typename element_type>
  static constexpr auto max(View::Vector<element_type> source) -> Count {
    const auto* data = source.get_data();
    Count target_index = 0;
    for (Count index = 1; index < source.get_size(); index++) {
      if (data[index] > data[target_index]) {
        target_index = index;
      }
    }

    return target_index;
  }
};

}  // namespace Perimortem::Core::Algorithm
