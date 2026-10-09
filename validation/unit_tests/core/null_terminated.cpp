// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/null_terminated.hpp"

#include "toolchain/validation/unit_test.hpp"

using namespace Perimortem::Core;
using namespace Toolchain::Validation;

static Harness NullTerminatedViews = {
  .name = "Core::NullTerminated",
};

VALIDATION_TEST(NullTerminatedViews, bounded_buffer) {
  const char buffer[] = {'a', 'b', 'c'};
  EXPECT_EQ(NullTerminated(buffer, sizeof(buffer)).get_view(), "abc"_view);
  EXPECT_EQ(NullTerminated(buffer, 2).get_view(), "ab"_view);
  EXPECT(NullTerminated(buffer + sizeof(buffer), 0).get_view().is_empty());
  EXPECT(NullTerminated(nullptr, sizeof(buffer)).get_view().is_empty());
}

VALIDATION_TEST(NullTerminatedViews, stops_at_null) {
  const char buffer[] = {'a', '\0', 'b', '\0'};
  EXPECT_EQ(NullTerminated(buffer, sizeof(buffer)).get_view(), "a"_view);
}

VALIDATION_TEST(NullTerminatedViews, exact_bytes) {
  const char buffer[] = {'a', '\0', 'b', '\0'};
  EXPECT_EQ(View::Bytes(buffer, sizeof(buffer)), "a\0b\0"_view);
}
