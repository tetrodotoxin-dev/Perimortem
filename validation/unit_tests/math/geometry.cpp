// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/math/domain.hpp"
#include "perimortem/math/volume.hpp"

#include "toolchain/validation/unit_test.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Math;
using namespace Toolchain::Validation;

static Harness MathGeometry = {.name = "Perimortem::Math::Geometry"};

VALIDATION_TEST(MathGeometry, clipped_size) {
  Size<R32, 2> size({{-3.0f, 4.0f}});
  EXPECT_EQ(size[0], 0.0f);
  EXPECT_EQ(size[1], 4.0f);
  EXPECT(size.is_empty());

  auto checked = Size<R32, 2>::create({{-3.0f, 4.0f}});
  Bool clipped = False;
  checked.visit(
      [](const Size<R32, 2>&) {},
      [&](Size<R32, 2>::Failure failure) {
        clipped = failure == Size<R32, 2>::Failure::Clipped;
      });
  EXPECT(clipped);
}

VALIDATION_TEST(MathGeometry, nonfinite_size) {
  R32 invalid = __builtin_nanf("");
  Size<R32, 2> size({{invalid, 2.0f}});
  EXPECT_EQ(size[0], 0.0f);
  auto checked = Size<R32, 2>::create({{invalid, 2.0f}});
  Bool nonfinite = False;
  checked.visit(
      [](const Size<R32, 2>&) {},
      [&](Size<R32, 2>::Failure failure) {
        nonfinite = failure == Size<R32, 2>::Failure::NonFinite;
      });
  EXPECT(nonfinite);
}

VALIDATION_TEST(MathGeometry, exact_size) {
  auto checked = Size<R32, 2>::create({{3.0f, 4.0f}});
  Bool exact = False;
  checked.visit(
      [&](const Size<R32, 2>& size) {
        exact = size[0] == 3.0f && size[1] == 4.0f;
      },
      [](Size<R32, 2>::Failure) {});
  EXPECT(exact);
}

VALIDATION_TEST(MathGeometry, domain_extent) {
  Domain<2> domain(Size<U32, 2>({{3, 2}}));
  EXPECT(domain.contains(Point<U32, 2>({{2, 1}})));
  EXPECT_NOT(domain.contains(Point<U32, 2>({{3, 1}})));
  EXPECT_NOT(domain.contains(Point<S64, 2>({{-1, 1}})));
  auto count = domain.get_element_count();
  ASSERT(count);
  EXPECT_EQ(*count, 6);
  Domain<3> huge(Size<U32, 3>({{~U32{}, ~U32{}, ~U32{}}}));
  EXPECT_NOT(huge.get_element_count());
}

VALIDATION_TEST(MathGeometry, clipped_domain) {
  Domain<1> domain(Size<U32, 1>({{3}}));
  EXPECT_NOT(domain.resolve(Point<S64, 1>({{-1}})));
  EXPECT_NOT(domain.resolve(Point<S64, 1>({{3}})));
  auto inside = domain.resolve(Point<S64, 1>({{2}}));
  ASSERT(inside);
  EXPECT_EQ((*inside)[0], 2);
  auto limits = domain.get_limits();
  EXPECT(limits.contains(Point<S64, 1>({{2}})));
  EXPECT_NOT(limits.contains(Point<S64, 1>({{3}})));
}

VALIDATION_TEST(MathGeometry, mixed_boundaries) {
  Domain<2, Boundary::Wrap, Boundary::Saturate> domain(
      Size<U32, 2>({{3, 2}}));
  auto mapped = domain.resolve(Point<S64, 2>({{-1, 9}}));
  ASSERT(mapped);
  EXPECT_EQ((*mapped)[0], 2);
  EXPECT_EQ((*mapped)[1], 1);
  auto lower = domain.resolve(Point<S64, 2>({{3, -9}}));
  ASSERT(lower);
  EXPECT_EQ((*lower)[0], 0);
  EXPECT_EQ((*lower)[1], 0);
  constexpr S64 minimum = -9223372036854775807LL - 1;
  auto extreme = domain.resolve(Point<S64, 2>({{minimum, 0}}));
  ASSERT(extreme);
  EXPECT_EQ((*extreme)[0], 1);
}

VALIDATION_TEST(MathGeometry, uniform_boundary) {
  Domain<2, Boundary::Wrap> domain(Size<U32, 2>({{3, 2}}));
  auto mapped = domain.resolve(Point<S64, 2>({{-1, -1}}));
  ASSERT(mapped);
  EXPECT_EQ((*mapped)[0], 2);
  EXPECT_EQ((*mapped)[1], 1);
}

VALIDATION_TEST(MathGeometry, mirrored_domain) {
  Domain<1, Boundary::Mirror> domain(Size<U32, 1>({{3}}));
  auto before = domain.resolve(Point<S64, 1>({{-1}}));
  auto edge = domain.resolve(Point<S64, 1>({{3}}));
  auto reflected = domain.resolve(Point<S64, 1>({{4}}));
  ASSERT(before && edge && reflected);
  EXPECT_EQ((*before)[0], 0);
  EXPECT_EQ((*edge)[0], 2);
  EXPECT_EQ((*reflected)[0], 1);
}

VALIDATION_TEST(MathGeometry, empty_domain_boundary) {
  Static::Vector<U32, 1> extent;
  Domain<1, Boundary::Wrap> domain{Size<U32, 1>(extent)};
  EXPECT_NOT(domain.resolve(Point<S64, 1>({{-1}})));
}

VALIDATION_TEST(MathGeometry, volume_edges) {
  Volume<S32, 2> volume(
      Point<S32, 2>({{-2147483647 - 1, 0}}),
      Size<S32, 2>({{2147483647, 2}}));
  EXPECT(volume.contains(Point<S32, 2>({{-2147483647 - 1, 0}})));
  EXPECT_NOT(volume.contains(Point<S32, 2>({{2147483647, 0}})));
  EXPECT_NOT(volume.contains(Point<S32, 2>({{-2147483647 - 1, 2}})));
}

VALIDATION_TEST(MathGeometry, volume_wide_edges) {
  constexpr S64 minimum = -9223372036854775807LL - 1;
  constexpr S64 maximum = 9223372036854775807LL;
  Volume<S64, 2> volume(
      Point<S64, 2>({{minimum, 0}}),
      Size<S64, 2>({{maximum, 1}}));
  EXPECT(volume.contains(Point<S64, 2>({{minimum, 0}})));
  EXPECT(volume.contains(Point<S64, 2>({{-2, 0}})));
  EXPECT_NOT(volume.contains(Point<S64, 2>({{-1, 0}})));
  EXPECT_NOT(volume.contains(Point<S64, 2>({{maximum, 0}})));
}

VALIDATION_TEST(MathGeometry, volume_three_axes) {
  Volume<R32, 3> volume(
      Point<R32, 3>({{-2.0f, 1.0f, 4.0f}}),
      Size<R32, 3>({{3.0f, 2.0f, 5.0f}}));
  EXPECT(volume.contains(Point<R32, 3>({{0.0f, 2.0f, 8.0f}})));
  EXPECT_NOT(volume.contains(Point<R32, 3>({{1.0f, 2.0f, 8.0f}})));
  EXPECT_NOT(volume.contains(Point<R32, 3>({{0.0f, 3.0f, 8.0f}})));
  EXPECT_NOT(volume.contains(Point<R32, 3>({{0.0f, 2.0f, 9.0f}})));
}
