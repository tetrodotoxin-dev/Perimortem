// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/graphics/image.hpp"

#include "toolchain/validation/unit_test.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Graphics;
using namespace Perimortem::Memory;
using namespace Toolchain::Validation;

static Harness GraphicsImage = {.name = "Graphics::Image"};

VALIDATION_TEST(GraphicsImage, empty_values) {
  const Count requests = Bibliotheca::check_out_requests();
  Image image;
  EXPECT_EQ(Bibliotheca::check_out_requests(), requests);
  EXPECT_NOT(image.is_drawable());
  EXPECT(image.get_pixels().is_empty());
}

// A codec hands its finished allocation to Image. Neither adoption nor moving
// the Image itself needs another allocation or a pixel copy.
VALIDATION_TEST(GraphicsImage, adopts_pixels) {
  Dynamic::Vector<Pixel> pixels;
  pixels.emplace(Pixel::from_rgba(17, 34, 51, 68));
  const auto* data = pixels.get_data();
  const Count requests = Bibliotheca::check_out_requests();
  auto image = Image::create(Size2D(1, 1), Data::take(pixels));
  ASSERT(image);
  Image moved(Data::take(*image));
  EXPECT_EQ(Bibliotheca::check_out_requests(), requests);
  EXPECT_EQ(moved.get_pixels().get_data(), data);
  EXPECT_NOT(image->is_drawable());
  EXPECT_EQ(image->get_width(), U32(0));
  EXPECT_EQ(moved.get_pixel(0, 0).red, U8(17));
  EXPECT_EQ(moved.get_pixel(-1, 0).alpha, U8(0));
  EXPECT_EQ(moved.get_pixel(1, 0).alpha, U8(0));
}

VALIDATION_TEST(GraphicsImage, rectangle_moves) {
  auto first_image = Image::create(Size2D(2, 3), Dynamic::Vector<Pixel>());
  auto second_image = Image::create(Size2D(4, 5), Dynamic::Vector<Pixel>());
  ASSERT(first_image && second_image);
  Image& source = *first_image;
  Image& destination = *second_image;
  const auto* first = source.get_pixels().get_data();
  const auto* second = destination.get_pixels().get_data();
  destination = Data::take(source);
  EXPECT_EQ(destination.get_pixels().get_data(), first);
  EXPECT_EQ(destination.get_width(), U32(2));
  EXPECT_EQ(destination.get_height(), U32(3));
  EXPECT_EQ(source.get_pixels().get_data(), second);
  EXPECT_EQ(source.get_width(), U32(4));
  EXPECT_EQ(source.get_height(), U32(5));

  auto& same = destination;
  destination = Data::take(same);
  EXPECT_EQ(destination.get_pixels().get_data(), first);
  EXPECT(destination.is_drawable());
}

// A copied input remains the caller's storage. Padding belongs to the new
// image, including transparent alpha rather than just black RGB channels.
VALIDATION_TEST(GraphicsImage, copies_and_pads) {
  Dynamic::Vector<Pixel> pixels;
  pixels.emplace(Pixel::from_rgb(1, 2, 3));
  auto image = Image::create(Size2D(2, 1), pixels);
  ASSERT(image);
  EXPECT_EQ(pixels.get_size(), Count(1));
  EXPECT(image->get_pixels().get_data() != pixels.get_data());
  EXPECT_EQ(image->get_pixel(0, 0).alpha, U8(255));
  const auto padded = image->get_pixel(1, 0);
  EXPECT(
      padded.red == 0 && padded.green == 0 && padded.blue == 0 &&
      padded.alpha == 0);
  EXPECT_EQ(image->get_pixels().get_size(), Count(2));
}

// Fitting a smaller rectangle changes the visible extent, not the allocation.
// Pointer identity and allocator traffic catch an accidental replacement copy.
VALIDATION_TEST(GraphicsImage, oversized_buffer) {
  Dynamic::Vector<Pixel> pixels;
  pixels.resize(32);
  pixels[0] = Pixel::from_rgb(1, 2, 3);
  const auto* data = pixels.get_data();
  const Count requests = Bibliotheca::check_out_requests();
  auto image = Image::create(Size2D(1, 1), Data::take(pixels));
  ASSERT(image);
  EXPECT_EQ(Bibliotheca::check_out_requests(), requests);
  EXPECT_EQ(image->get_pixels().get_data(), data);
  EXPECT_EQ(image->get_pixels().get_size(), Count(1));
  EXPECT_EQ(image->get_pixel(0, 0).red, U8(1));
  EXPECT_EQ(image->get_pixel(1, 0).alpha, U8(0));
}

// These extents fail before fitting or allocating a buffer. The large element
// count fits in Count, but its RGBA byte extent would wrap to zero.
VALIDATION_TEST(GraphicsImage, invalid_dimensions) {
  const Count requests = Bibliotheca::check_out_requests();
  EXPECT_NOT(Image::create(Size2D(0, 1), Dynamic::Vector<Pixel>()));
  EXPECT_NOT(Image::create(Size2D(1, 0), Dynamic::Vector<Pixel>()));
  EXPECT_NOT(
      Image::create(
          Size2D(U32(1) << 31, U32(1) << 31), Dynamic::Vector<Pixel>()));
  EXPECT_EQ(Bibliotheca::check_out_requests(), requests);
}
