// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.hpp"

namespace Perimortem::Serialization {

// Rgba8 stores four sample bytes in red, green, blue, alpha order. PNG expands
// other channel layouts to this value before returning a decoded matrix. Color
// samples retain their decoded byte values, while missing alpha becomes fully
// opaque. Storage alone does not establish a transfer function or a rendering
// color. Public channels let the codec write a continuous buffer while the
// four byte layout stays visible to callers. The sample requires only byte
// alignment because PNG filtering addresses its channels as bytes.
class Rgba8 {
 public:
  // Default member initialization leaves every channel zero, giving buffers a
  // transparent black zero state without constructor work.
  Rgba8() = default;

  // Red, green, and blue channels all get set to the same grey value and do not
  // compensate for luminance. The alpha channel is independently set to full
  // opacity.
  static constexpr auto from_grey(U8 grey) -> Rgba8 {
    return from_rgba(grey, grey, grey, opaque);
  }

  // Replicates the decoded grey sample without luminance conversion and
  // preserves the caller supplied alpha byte.
  static constexpr auto from_grey_alpha(U8 grey, U8 alpha) -> Rgba8 {
    return from_rgba(grey, grey, grey, alpha);
  }

  // Preserves the three decoded color samples and supplies full opacity for the
  // missing alpha channel.
  static constexpr auto from_rgb(U8 red, U8 green, U8 blue) -> Rgba8 {
    return from_rgba(red, green, blue, opaque);
  }

  // Preserves all four decoded samples in public channel order.
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
