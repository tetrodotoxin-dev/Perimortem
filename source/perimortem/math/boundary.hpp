// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/perimortem.hpp"

namespace Perimortem::Math {

// Boundary chooses what one finite axis does with a coordinate outside its
// limits. Clip refuses it, Saturate selects the nearest endpoint, Mirror
// reflects with the edge sample repeated, and Wrap repeats the axis.
enum class Boundary : U8 {
  Clip,
  Saturate,
  Mirror,
  Wrap,
};

}  // namespace Perimortem::Math
