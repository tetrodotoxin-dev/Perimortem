// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/serialization/formats/rgba8.hpp"

#include "toolchain/validation/test.hpp"

using namespace Perimortem::Serialization;
using namespace Toolchain::Validation;

static Harness SerializationRgba8 = {.name = "Serialization::Formats::Rgba8"};

VALIDATION_TEST(SerializationRgba8, channel_factories) {
  Formats::Rgba8 grey = Formats::Rgba8::from_grey(0x22);
  Formats::Rgba8 grey_alpha = Formats::Rgba8::from_grey_alpha(0x33, 0x44);
  Formats::Rgba8 rgb = Formats::Rgba8::from_rgb(0x55, 0x66, 0x77);
  Formats::Rgba8 rgba = Formats::Rgba8::from_rgba(0x88, 0x99, 0xAA, 0xBB);

  EXPECT_EQ(grey.red, 0x22);
  EXPECT_EQ(grey.alpha, 0xFF);
  EXPECT_EQ(grey_alpha.green, 0x33);
  EXPECT_EQ(grey_alpha.alpha, 0x44);
  EXPECT_EQ(rgb.blue, 0x77);
  EXPECT_EQ(rgb.alpha, 0xFF);
  EXPECT_EQ(rgba.red, 0x88);
  EXPECT_EQ(rgba.alpha, 0xBB);
}
