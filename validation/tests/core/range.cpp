// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/math/range.hpp"

#include "toolchain/validation/test.hpp"

using namespace Perimortem::Core;
using namespace Toolchain::Validation;

static Harness CoreMathRange = {
  .name = "Core::Math::Range",
};

VALIDATION_TEST(CoreMathRange, overlap) {
  constexpr Math::Range source = {2, 4};
  constexpr Math::Range invalid = {Count(-1), 1};
  EXPECT(source.has_overlap({5, 3}));
  EXPECT_NOT(source.has_overlap({6, 3}));
  EXPECT_NOT(Math::Range().has_overlap({0, 1}));
  EXPECT_NOT(invalid.has_overlap(invalid));
}

VALIDATION_TEST(CoreMathRange, include) {
  Math::Range range;
  EXPECT(range.include(4));
  EXPECT(range.include(7));
  EXPECT(range.include(2));

  EXPECT_EQ(range.start, Count(2));
  EXPECT_EQ(range.size, Count(6));
}

VALIDATION_TEST(CoreMathRange, representable_end) {
  Math::Range range;
  EXPECT(range.include(Count(-2)));
  EXPECT_EQ(range.start, Count(-2));
  EXPECT_EQ(range.size, Count(1));

  const auto end = range.try_end();
  ASSERT(end);
  end.visit([&](Count value) { EXPECT_EQ(value, Count(-1)); }, []() {});

  EXPECT_NOT(range.include(Count(-1)));
  EXPECT_EQ(range.start, Count(-2));
  EXPECT_EQ(range.size, Count(1));

  Math::Range invalid = {Count(-1), 1};
  EXPECT_NOT(invalid.is_valid());
  EXPECT_NOT(invalid.try_end());
  EXPECT_NOT(invalid.include(0));
}
