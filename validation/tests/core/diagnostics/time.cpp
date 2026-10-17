// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/diagnostics/time.hpp"

#include "toolchain/validation/test.hpp"

using namespace Perimortem::Core;
using namespace Toolchain::Validation;

static Harness DiagnosticsTime = {
  .name = "Core::Diagnostics::Time",
};

VALIDATION_TEST(DiagnosticsTime, duration) {
  constexpr auto earlier = Diagnostics::Time::from_nanoseconds(10'250'000'000);
  constexpr auto later = Diagnostics::Time::from_nanoseconds(12'750'000'000);
  constexpr auto forward = earlier.measure(later);
  constexpr auto reverse = later.measure(earlier);

  EXPECT_EQ(forward.convert_to_nanoseconds(), U64(2'500'000'000));
  EXPECT_EQ(reverse.convert_to_nanoseconds(), U64(2'500'000'000));
  EXPECT_EQ(forward.convert_to_microseconds(), R64(2'500'000.0));
  EXPECT_EQ(forward.convert_to_milliseconds(), R64(2'500.0));
  EXPECT_EQ(forward.convert_to_seconds(), R64(2.5));
}

VALIDATION_TEST(DiagnosticsTime, ordering) {
  constexpr auto earlier = Diagnostics::Time::from_nanoseconds(100);
  constexpr auto later = Diagnostics::Time::from_nanoseconds(101);

  EXPECT(earlier < later);
  EXPECT_NOT(later < earlier);
  EXPECT(earlier == earlier);
  EXPECT_NOT(earlier == later);
}

VALIDATION_TEST(DiagnosticsTime, module_boot) {
  const auto boot = Diagnostics::Time::boot();
  const auto current = Diagnostics::Time::now();

  EXPECT(boot == current || boot < current);
}
