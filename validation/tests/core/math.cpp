// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/core/math/domain.hpp"
#include "perimortem/core/math/scalar.hpp"
#include "perimortem/core/math/volume.hpp"

using namespace Perimortem::Core;
using namespace Toolchain::Validation;

static Harness CoreMath = {.name = "Core::Math"};

VALIDATION_TEST(CoreMath, scalar_ordering) {
  EXPECT_EQ(Math::Scalar::min(S32(4), S32(7)), S32(4));
  EXPECT_EQ(Math::Scalar::max(S32(4), S32(7)), S32(7));
  EXPECT_EQ(Math::Scalar::clamp(S32(9), S32(2), S32(6)), S32(6));
}

VALIDATION_TEST(CoreMath, scalar_wrap) {
  EXPECT_EQ(Math::Scalar::wrap(S32(5), S32(3)), S32(2));
  EXPECT_EQ(Math::Scalar::wrap(S32(-1), S32(3)), S32(2));
}

VALIDATION_TEST(CoreMath, scalar_magnitude) {
  constexpr S64 signed_minimum = -9223372036854775807LL - 1;

  EXPECT_EQ(Math::Scalar::magnitude<U8>(S8(-1)), U8(1));
  EXPECT_EQ(Math::Scalar::magnitude<U8>(S8(-128)), U8(128));
  EXPECT_EQ(Math::Scalar::magnitude<U64>(signed_minimum), U64(1) << 63);
  EXPECT_EQ(Math::Scalar::magnitude<U64>(S8(-128)), U64(128));
  EXPECT_EQ(Math::Scalar::magnitude<U64>(U16(65535)), U64(65535));
}

VALIDATION_TEST(CoreMath, scalar_bit_width) {
  EXPECT_EQ(Math::Scalar::bit_width(U64(0)), U64(0));
  EXPECT_EQ(Math::Scalar::bit_width(U64(1)), U64(1));
  EXPECT_EQ(Math::Scalar::bit_width(U64(2)), U64(2));
  EXPECT_EQ(Math::Scalar::bit_width(U64(3)), U64(2));
  EXPECT_EQ(Math::Scalar::bit_width(U64(-1)), U64(64));
}

VALIDATION_TEST(CoreMath, scalar_reals) {
  const R64 root = Math::Scalar::sqrt(9.0);
  const R64 floor_positive = Math::Scalar::floor(1.75);
  const R64 floor_negative = Math::Scalar::floor(-1.25);
  const R64 ceil_positive = Math::Scalar::ceil(1.25);
  const R64 ceil_negative = Math::Scalar::ceil(-1.25);

  EXPECT_EQ(root, 3.0);
  EXPECT_EQ(floor_positive, 1.0);
  EXPECT_EQ(floor_negative, -2.0);
  EXPECT_EQ(ceil_positive, 2.0);
  EXPECT_EQ(ceil_negative, -1.0);
}

VALIDATION_TEST(CoreMath, clipped_size) {
  Math::Size<R32, 2> size({{-3.0f, 4.0f}});
  EXPECT_EQ(size[0], 0.0f);
  EXPECT_EQ(size[1], 4.0f);
  EXPECT(size.is_empty());
}

VALIDATION_TEST(CoreMath, nonfinite_size) {
  R32 invalid = __builtin_nanf("");
  Math::Size<R32, 2> size({{invalid, 2.0f}});
  EXPECT_EQ(size[0], 0.0f);
}

VALIDATION_TEST(CoreMath, exact_size) {
  Math::Size<R32, 2> size({{3.0f, 4.0f}});
  EXPECT_EQ(size[0], 3.0f);
  EXPECT_EQ(size[1], 4.0f);
}

VALIDATION_TEST(CoreMath, domain_extent) {
  Math::Domain<2> domain(Math::Size<U32, 2>({{3, 2}}));
  EXPECT(domain.contains(Math::Point<U32, 2>({{2, 1}})));
  EXPECT_NOT(domain.contains(Math::Point<U32, 2>({{3, 1}})));
  EXPECT_NOT(domain.contains(Math::Point<S64, 2>({{-1, 1}})));

  auto count = domain.get_element_count();
  ASSERT(count);
  count.visit([&](Count value) { EXPECT_EQ(value, 6); }, []() {});

  Math::Domain<3> huge(Math::Size<U32, 3>({{~U32{}, ~U32{}, ~U32{}}}));
  EXPECT_NOT(huge.get_element_count());
}

VALIDATION_TEST(CoreMath, clipped_domain) {
  Math::Domain<1> domain(Math::Size<U32, 1>({{3}}));
  EXPECT_NOT(domain.resolve(Math::Point<S64, 1>({{-1}})));
  EXPECT_NOT(domain.resolve(Math::Point<S64, 1>({{3}})));

  auto inside = domain.resolve(Math::Point<S64, 1>({{2}}));
  ASSERT(inside);
  inside.visit(
      [&](const Math::Point<U32, 1>& point) { EXPECT_EQ(point[0], 2); },
      []() {});

  auto limits = domain.get_limits();
  EXPECT(limits.contains(Math::Point<S64, 1>({{2}})));
  EXPECT_NOT(limits.contains(Math::Point<S64, 1>({{3}})));
}

VALIDATION_TEST(CoreMath, mixed_boundaries) {
  Math::Domain<2, Math::Boundary::Wrap, Math::Boundary::Saturate> domain(
      Math::Size<U32, 2>({{3, 2}}));
  auto mapped = domain.resolve(Math::Point<S64, 2>({{-1, 9}}));
  ASSERT(mapped);
  mapped.visit(
      [&](const Math::Point<U32, 2>& point) {
        EXPECT_EQ(point[0], 2);
        EXPECT_EQ(point[1], 1);
      },

      []() {});

  auto lower = domain.resolve(Math::Point<S64, 2>({{3, -9}}));
  ASSERT(lower);
  lower.visit(
      [&](const Math::Point<U32, 2>& point) {
        EXPECT_EQ(point[0], 0);
        EXPECT_EQ(point[1], 0);
      },

      []() {});

  constexpr S64 minimum = -9223372036854775807LL - 1;
  auto extreme = domain.resolve(Math::Point<S64, 2>({{minimum, 0}}));
  ASSERT(extreme);
  extreme.visit(
      [&](const Math::Point<U32, 2>& point) { EXPECT_EQ(point[0], 1); },
      []() {});
}

VALIDATION_TEST(CoreMath, uniform_boundary) {
  Math::Domain<2, Math::Boundary::Wrap> domain(Math::Size<U32, 2>({{3, 2}}));
  auto mapped = domain.resolve(Math::Point<S64, 2>({{-1, -1}}));
  ASSERT(mapped);
  mapped.visit(
      [&](const Math::Point<U32, 2>& point) {
        EXPECT_EQ(point[0], 2);
        EXPECT_EQ(point[1], 1);
      },

      []() {});
}

VALIDATION_TEST(CoreMath, mirrored_domain) {
  Math::Domain<1, Math::Boundary::Mirror> domain(Math::Size<U32, 1>({{3}}));
  auto before = domain.resolve(Math::Point<S64, 1>({{-1}}));
  auto edge = domain.resolve(Math::Point<S64, 1>({{3}}));
  auto reflected = domain.resolve(Math::Point<S64, 1>({{4}}));
  ASSERT(before && edge && reflected);
  before.visit(
      [&](const Math::Point<U32, 1>& point) { EXPECT_EQ(point[0], 0); },
      []() {});
  edge.visit(
      [&](const Math::Point<U32, 1>& point) { EXPECT_EQ(point[0], 2); },
      []() {});
  reflected.visit(
      [&](const Math::Point<U32, 1>& point) { EXPECT_EQ(point[0], 1); },
      []() {});
}

VALIDATION_TEST(CoreMath, empty_domain_boundary) {
  Storage::Vector<U32, 1> extent;
  Math::Domain<1, Math::Boundary::Wrap> domain{Math::Size<U32, 1>(extent)};
  EXPECT_NOT(domain.resolve(Math::Point<S64, 1>({{-1}})));
}

VALIDATION_TEST(CoreMath, volume_edges) {
  Math::Volume<S32, 2> volume(
      Math::Point<S32, 2>({{-2147483647 - 1, 0}}),
      Math::Size<S32, 2>({{2147483647, 2}}));
  EXPECT(volume.contains(Math::Point<S32, 2>({{-2147483647 - 1, 0}})));
  EXPECT_NOT(volume.contains(Math::Point<S32, 2>({{2147483647, 0}})));
  EXPECT_NOT(volume.contains(Math::Point<S32, 2>({{-2147483647 - 1, 2}})));
}

VALIDATION_TEST(CoreMath, volume_wide_edges) {
  constexpr S64 minimum = -9223372036854775807LL - 1;
  constexpr S64 maximum = 9223372036854775807LL;
  Math::Volume<S64, 2> volume(
      Math::Point<S64, 2>({{minimum, 0}}), Math::Size<S64, 2>({{maximum, 1}}));
  EXPECT(volume.contains(Math::Point<S64, 2>({{minimum, 0}})));
  EXPECT(volume.contains(Math::Point<S64, 2>({{-2, 0}})));
  EXPECT_NOT(volume.contains(Math::Point<S64, 2>({{-1, 0}})));
  EXPECT_NOT(volume.contains(Math::Point<S64, 2>({{maximum, 0}})));
}

VALIDATION_TEST(CoreMath, volume_three_axes) {
  Math::Volume<R32, 3> volume(
      Math::Point<R32, 3>({{-2.0f, 1.0f, 4.0f}}),
      Math::Size<R32, 3>({{3.0f, 2.0f, 5.0f}}));
  EXPECT(volume.contains(Math::Point<R32, 3>({{0.0f, 2.0f, 8.0f}})));
  EXPECT_NOT(volume.contains(Math::Point<R32, 3>({{1.0f, 2.0f, 8.0f}})));
  EXPECT_NOT(volume.contains(Math::Point<R32, 3>({{0.0f, 3.0f, 8.0f}})));
  EXPECT_NOT(volume.contains(Math::Point<R32, 3>({{0.0f, 2.0f, 9.0f}})));
}
