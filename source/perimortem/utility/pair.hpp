// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

namespace Perimortem::Utility {

// Pair names two related values for local transformations and compile time
// table declarations without adding lookup, ordering, or lifetime behavior.
template <typename key_type, typename value_type>
struct Pair {
  key_type key;
  value_type value;
};

}  // namespace Perimortem::Utility
