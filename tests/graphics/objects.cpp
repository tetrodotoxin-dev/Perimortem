// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/unit_test.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/null_terminated.hpp"

#include "perimortem/memory/dynamic/vector.hpp"

#include "perimortem/graphics/image.hpp"
#include "perimortem/graphics/pixel.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Graphics;
using namespace Perimortem::Memory;
using namespace Toolchain::Validation;

static Harness GraphicsObjects = {
  .name = "Perimortem::Graphics::Objects",
};

static auto one_pixel_image() -> Image {
  Dynamic::Vector<Pixel> pixels;
  pixels.emplace(Pixel::from_rgba(0x12, 0x34, 0x56, 0x78));
  return Image::create(Size2D(1, 1), Data::take(pixels))
      .visit(
          [] { return Image(); },
          [](Image& image) { return Data::take(image); });
}

VALIDATION_TEST(GraphicsObjects, image_copies_pixels) {
  Image empty;
  EXPECT_NOT(empty.is_drawable());
  EXPECT(empty.get_pixels().is_empty());

  Image image = one_pixel_image();
  Image alias = image;
  EXPECT(image.is_drawable());
  EXPECT_EQ(image.get_size_pixels().width, U32(1));
  EXPECT_EQ(image.get_size_pixels().height, U32(1));
  EXPECT(image.get_pixels().get_data() != alias.get_pixels().get_data());
  image = Image();
  EXPECT_EQ(alias.get_pixel(0, 0).red, U8(0x12));
}

VALIDATION_TEST(GraphicsObjects, pixel_factories) {
  Pixel grey = Pixel::from_grey(0x22);
  Pixel grey_alpha = Pixel::from_grey_alpha(0x33, 0x44);
  Pixel rgb = Pixel::from_rgb(0x55, 0x66, 0x77);
  Pixel rgba = Pixel::from_rgba(0x88, 0x99, 0xAA, 0xBB);

  EXPECT_EQ(grey.red, U8(0x22));
  EXPECT_EQ(grey.alpha, U8(0xFF));
  EXPECT_EQ(grey_alpha.green, U8(0x33));
  EXPECT_EQ(grey_alpha.alpha, U8(0x44));
  EXPECT_EQ(rgb.blue, U8(0x77));
  EXPECT_EQ(rgb.alpha, U8(0xFF));
  EXPECT_EQ(rgba.red, U8(0x88));
  EXPECT_EQ(rgba.alpha, U8(0xBB));
}
