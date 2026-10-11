// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/time.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/null_terminated.hpp"

using namespace Perimortem::Core;
using namespace Toolchain::Validation;

static Harness CoreTime = {
  .name = "Core::Time",
};

VALIDATION_TEST(CoreTime, duration) {
  constexpr Time earlier(10, 250'000'000);
  constexpr Time later(12, 750'000'000);
  constexpr auto forward = earlier.measure(later);
  constexpr auto reverse = later.measure(earlier);

  EXPECT_EQ(forward.convert_to_nanoseconds(), U64(2'500'000'000));
  EXPECT_EQ(reverse.convert_to_nanoseconds(), U64(2'500'000'000));
  EXPECT_EQ(forward.convert_to_microseconds(), R64(2'500'000.0));
  EXPECT_EQ(forward.convert_to_milliseconds(), R64(2'500.0));
  EXPECT_EQ(forward.convert_to_seconds(), R64(2.5));
}

VALIDATION_TEST(CoreTime, clock_format) {
  constexpr Time stamp(3'723, 4'000'000);

  EXPECT_EQ(stamp.calculate_clock(), "01:02:03.004"_view);
}
