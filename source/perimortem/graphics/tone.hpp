// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/perimortem.hpp"

namespace Perimortem::Graphics {

// Tone represents a full color range in floats. Values greater than 1 and less
// than zero are valid.
struct Tone {
  R32 red = 1.0;
  R32 green = 1.0;
  R32 blue = 1.0;
  R32 alpha = 1.0;
};

static_assert(sizeof(Tone) == sizeof(R32) * 4);
static_assert(__is_standard_layout(Tone));

}  // namespace Perimortem::Graphics
