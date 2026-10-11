// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/utility/range.hpp"

#include "toolchain/validation/test.hpp"

using namespace Perimortem::Utility;
using namespace Toolchain::Validation;

static Harness UtilityRange = {
  .name = "Utility::Range",
};

VALIDATION_TEST(UtilityRange, overlap) {
  constexpr Range source = {2, 4};
  constexpr Range invalid = {Count(-1), 1};
  EXPECT(source.has_overlap({5, 3}));
  EXPECT_NOT(source.has_overlap({6, 3}));
  EXPECT_NOT(Range().has_overlap({0, 1}));
  EXPECT_NOT(invalid.has_overlap(invalid));
}

VALIDATION_TEST(UtilityRange, extend) {
  Range range;
  EXPECT(range.extend(4));
  EXPECT(range.extend(7));
  EXPECT(range.extend(2));

  EXPECT_EQ(range.start, Count(2));
  EXPECT_EQ(range.size, Count(6));
}

VALIDATION_TEST(UtilityRange, representable_end) {
  Range range;
  EXPECT(range.extend(Count(-2)));
  EXPECT_EQ(range.start, Count(-2));
  EXPECT_EQ(range.size, Count(1));

  const auto end = range.get_end();
  ASSERT(end);
  EXPECT_EQ(*end, Count(-1));

  EXPECT_NOT(range.extend(Count(-1)));
  EXPECT_EQ(range.start, Count(-2));
  EXPECT_EQ(range.size, Count(1));

  Range invalid = {Count(-1), 1};
  EXPECT_NOT(invalid.is_valid());
  EXPECT_NOT(invalid.get_end());
  EXPECT_NOT(invalid.extend(0));
}
