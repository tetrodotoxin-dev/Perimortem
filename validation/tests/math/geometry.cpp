// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/math/domain.hpp"
#include "perimortem/math/volume.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Math;
using namespace Toolchain::Validation;

static Harness MathGeometry = {.name = "Perimortem::Math::Geometry"};

VALIDATION_TEST(MathGeometry, clipped_size) {
  Size<R32, 2> size({{-3.0f, 4.0f}});
  EXPECT_EQ(size[0], 0.0f);
  EXPECT_EQ(size[1], 4.0f);
  EXPECT(size.is_empty());
}

VALIDATION_TEST(MathGeometry, nonfinite_size) {
  R32 invalid = __builtin_nanf("");
  Size<R32, 2> size({{invalid, 2.0f}});
  EXPECT_EQ(size[0], 0.0f);
}

VALIDATION_TEST(MathGeometry, exact_size) {
  Size<R32, 2> size({{3.0f, 4.0f}});
  EXPECT_EQ(size[0], 3.0f);
  EXPECT_EQ(size[1], 4.0f);
}

VALIDATION_TEST(MathGeometry, domain_extent) {
  Domain<2> domain(Size<U32, 2>({{3, 2}}));
  EXPECT(domain.contains(Point<U32, 2>({{2, 1}})));
  EXPECT_NOT(domain.contains(Point<U32, 2>({{3, 1}})));
  EXPECT_NOT(domain.contains(Point<S64, 2>({{-1, 1}})));

  auto count = domain.get_element_count();
  ASSERT(count);
  count.visit([&](Count value) { EXPECT_EQ(value, 6); }, []() {});

  Domain<3> huge(Size<U32, 3>({{~U32{}, ~U32{}, ~U32{}}}));
  EXPECT_NOT(huge.get_element_count());
}

VALIDATION_TEST(MathGeometry, clipped_domain) {
  Domain<1> domain(Size<U32, 1>({{3}}));
  EXPECT_NOT(domain.resolve(Point<S64, 1>({{-1}})));
  EXPECT_NOT(domain.resolve(Point<S64, 1>({{3}})));

  auto inside = domain.resolve(Point<S64, 1>({{2}}));
  ASSERT(inside);
  inside.visit(
      [&](const Point<U32, 1>& point) { EXPECT_EQ(point[0], 2); }, []() {});

  auto limits = domain.get_limits();
  EXPECT(limits.contains(Point<S64, 1>({{2}})));
  EXPECT_NOT(limits.contains(Point<S64, 1>({{3}})));
}

VALIDATION_TEST(MathGeometry, mixed_boundaries) {
  Domain<2, Boundary::Wrap, Boundary::Saturate> domain(Size<U32, 2>({{3, 2}}));
  auto mapped = domain.resolve(Point<S64, 2>({{-1, 9}}));
  ASSERT(mapped);
  mapped.visit(
      [&](const Point<U32, 2>& point) {
        EXPECT_EQ(point[0], 2);
        EXPECT_EQ(point[1], 1);
      },

      []() {});

  auto lower = domain.resolve(Point<S64, 2>({{3, -9}}));
  ASSERT(lower);
  lower.visit(
      [&](const Point<U32, 2>& point) {
        EXPECT_EQ(point[0], 0);
        EXPECT_EQ(point[1], 0);
      },

      []() {});

  constexpr S64 minimum = -9223372036854775807LL - 1;
  auto extreme = domain.resolve(Point<S64, 2>({{minimum, 0}}));
  ASSERT(extreme);
  extreme.visit(
      [&](const Point<U32, 2>& point) { EXPECT_EQ(point[0], 1); }, []() {});
}

VALIDATION_TEST(MathGeometry, uniform_boundary) {
  Domain<2, Boundary::Wrap> domain(Size<U32, 2>({{3, 2}}));
  auto mapped = domain.resolve(Point<S64, 2>({{-1, -1}}));
  ASSERT(mapped);
  mapped.visit(
      [&](const Point<U32, 2>& point) {
        EXPECT_EQ(point[0], 2);
        EXPECT_EQ(point[1], 1);
      },

      []() {});
}

VALIDATION_TEST(MathGeometry, mirrored_domain) {
  Domain<1, Boundary::Mirror> domain(Size<U32, 1>({{3}}));
  auto before = domain.resolve(Point<S64, 1>({{-1}}));
  auto edge = domain.resolve(Point<S64, 1>({{3}}));
  auto reflected = domain.resolve(Point<S64, 1>({{4}}));
  ASSERT(before && edge && reflected);
  before.visit(
      [&](const Point<U32, 1>& point) { EXPECT_EQ(point[0], 0); }, []() {});
  edge.visit(
      [&](const Point<U32, 1>& point) { EXPECT_EQ(point[0], 2); }, []() {});
  reflected.visit(
      [&](const Point<U32, 1>& point) { EXPECT_EQ(point[0], 1); }, []() {});
}

VALIDATION_TEST(MathGeometry, empty_domain_boundary) {
  Storage::Vector<U32, 1> extent;
  Domain<1, Boundary::Wrap> domain{Size<U32, 1>(extent)};
  EXPECT_NOT(domain.resolve(Point<S64, 1>({{-1}})));
}

VALIDATION_TEST(MathGeometry, volume_edges) {
  Volume<S32, 2> volume(
      Point<S32, 2>({{-2147483647 - 1, 0}}), Size<S32, 2>({{2147483647, 2}}));
  EXPECT(volume.contains(Point<S32, 2>({{-2147483647 - 1, 0}})));
  EXPECT_NOT(volume.contains(Point<S32, 2>({{2147483647, 0}})));
  EXPECT_NOT(volume.contains(Point<S32, 2>({{-2147483647 - 1, 2}})));
}

VALIDATION_TEST(MathGeometry, volume_wide_edges) {
  constexpr S64 minimum = -9223372036854775807LL - 1;
  constexpr S64 maximum = 9223372036854775807LL;
  Volume<S64, 2> volume(
      Point<S64, 2>({{minimum, 0}}), Size<S64, 2>({{maximum, 1}}));
  EXPECT(volume.contains(Point<S64, 2>({{minimum, 0}})));
  EXPECT(volume.contains(Point<S64, 2>({{-2, 0}})));
  EXPECT_NOT(volume.contains(Point<S64, 2>({{-1, 0}})));
  EXPECT_NOT(volume.contains(Point<S64, 2>({{maximum, 0}})));
}

VALIDATION_TEST(MathGeometry, volume_three_axes) {
  Volume<R32, 3> volume(
      Point<R32, 3>({{-2.0f, 1.0f, 4.0f}}), Size<R32, 3>({{3.0f, 2.0f, 5.0f}}));
  EXPECT(volume.contains(Point<R32, 3>({{0.0f, 2.0f, 8.0f}})));
  EXPECT_NOT(volume.contains(Point<R32, 3>({{1.0f, 2.0f, 8.0f}})));
  EXPECT_NOT(volume.contains(Point<R32, 3>({{0.0f, 3.0f, 8.0f}})));
  EXPECT_NOT(volume.contains(Point<R32, 3>({{0.0f, 2.0f, 9.0f}})));
}
