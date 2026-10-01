// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/perimortem.hpp"

namespace Perimortem::Serialization {

// Rgba8 stores four sample bytes in red, green, blue, alpha order. PNG expands
// other channel layouts to this value before publishing a decoded matrix. Color
// samples retain their decoded byte values, while missing alpha becomes fully
// opaque. Storage alone does not establish a transfer function or a rendering
// color. Public channels let the codec write a continuous buffer while the
// four byte layout stays visible to callers. The sample requires only byte
// alignment because PNG filtering addresses its channels as bytes.
class Rgba8 {
 public:
  // Fully transparent black is the zero state.
  Rgba8() = default;

  // Replicates grey to all color channels and uses full opacity.
  static constexpr auto from_grey(U8 grey) -> Rgba8 {
    return from_rgba(grey, grey, grey, opaque);
  }

  // Replicates grey to all color channels with an explicit alpha value.
  static constexpr auto from_grey_alpha(U8 grey, U8 alpha) -> Rgba8 {
    return from_rgba(grey, grey, grey, alpha);
  }

  // Stores three independent color channels and uses full opacity.
  static constexpr auto from_rgb(U8 red, U8 green, U8 blue) -> Rgba8 {
    return from_rgba(red, green, blue, opaque);
  }

  // Stores all four channels directly.
  static constexpr auto from_rgba(U8 red, U8 green, U8 blue, U8 alpha)
      -> Rgba8 {
    Rgba8 result;
    result.red = red;
    result.green = green;
    result.blue = blue;
    result.alpha = alpha;
    return result;
  }

  static constexpr auto get_bit_depth() -> Count { return 8; }
  static constexpr auto get_byte_count() -> Count { return 4; }
  static constexpr auto get_channel_count() -> U8 { return 4; }

  U8 red = 0;
  U8 green = 0;
  U8 blue = 0;
  U8 alpha = 0;

 private:
  static constexpr U8 opaque = 0xFF;
};

static_assert(sizeof(Rgba8) == 4);
static_assert(alignof(Rgba8) == alignof(U8));
static_assert(__is_standard_layout(Rgba8));

}  // namespace Perimortem::Serialization
