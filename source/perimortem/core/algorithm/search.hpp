// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/data.hpp"
#include "perimortem/core/scalar.hpp"
#include "perimortem/core/view/bytes.hpp"
#include "perimortem/core/view/vector.hpp"

namespace Perimortem::Core::Algorithm {

class Search {
 public:
  // Fast vectorized substring search for View::Bytes.
  static EXPORTED(PERIMORTEM) auto find(View::Bytes source, View::Bytes value)
      -> Count;

  // Fast vectorized byte search for View::Bytes.
  static EXPORTED(PERIMORTEM) auto find(View::Bytes source, U8 value) -> Count;

  // The first occurrence wins when several elements share the minimum value.
  template <typename element_type>
  static constexpr auto find_min(View::Vector<element_type> source) -> Count {
    const auto* data = source.get_data();
    Count target_index = 0;
    for (Count index = 1; index < source.get_size(); index++) {
      if (data[index] < data[target_index]) {
        target_index = index;
      }
    }

    return target_index;
  }

  // The first occurrence wins when several elements share the maximum value.
  template <typename element_type>
  static constexpr auto find_max(View::Vector<element_type> source) -> Count {
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
