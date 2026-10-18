// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

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

  // Returns the first index containing `value` or the terminal Count value when
  // the source contains no equal value.
  template <typename value_type>
  static constexpr auto match(
      View::Vector<value_type> source,
      const value_type& value) -> Count {
    const auto* data = source.get_data();
    for (Count index = 0; index < source.get_size(); index++) {
      if (data[index] == value) {
        return index;
      }
    }

    return Count(-1);
  }

  // Evaluates values in index order and returns the first index accepted by
  // `predicate`. Evaluation stops at that value. Rejecting every value returns
  // the terminal Count value.
  template <typename value_type, typename predicate_type>
    requires requires(predicate_type candidate, const value_type& value) {
      Bool(candidate(value));
    }
  static constexpr auto match(
      View::Vector<value_type> source,
      predicate_type predicate) -> Count {
    const auto* data = source.get_data();
    for (Count index = 0; index < source.get_size(); index++) {
      if (predicate(data[index])) {
        return index;
      }
    }

    return Count(-1);
  }

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
