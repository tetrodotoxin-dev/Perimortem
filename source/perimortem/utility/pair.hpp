// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

namespace Perimortem::Utility {

// Pair gives maps and compile time tables the same aggregate entry shape.
// Named `key` and `value` fields let those containers expose entries directly
// and keep their source declarations readable without separate proxy types.
template <typename key_type, typename value_type>
struct Pair {
  key_type key;
  value_type value;
};

}  // namespace Perimortem::Utility
