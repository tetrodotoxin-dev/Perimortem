// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/utility/range.hpp"

#include "toolchain/validation/unit_test.hpp"

using namespace Perimortem::Utility;
using namespace Toolchain::Validation;

static Harness UtilityRange = {
  .name = "Utility::Range",
};

VALIDATION_TEST(UtilityRange, overlap) {
  constexpr Range source = {2, 4};
  EXPECT(source.has_overlap({5, 3}));
  EXPECT_NOT(source.has_overlap({6, 3}));
  EXPECT_NOT(Range().has_overlap({0, 1}));
}

VALIDATION_TEST(UtilityRange, extend) {
  Range range;
  range.extend(4);
  range.extend(7);
  range.extend(2);

  EXPECT_EQ(range.start, Count(2));
  EXPECT_EQ(range.size, Count(6));
}
