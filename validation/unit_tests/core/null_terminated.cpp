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
  EXPECT_EQ(
      NullTerminated::convert_cstring(buffer, sizeof(buffer)), "abc"_view);
  EXPECT_EQ(NullTerminated::convert_cstring(buffer, 2), "ab"_view);
  EXPECT(
      NullTerminated::convert_cstring(buffer + sizeof(buffer), 0).is_empty());
  EXPECT(NullTerminated::convert_cstring(nullptr, sizeof(buffer)).is_empty());
}

VALIDATION_TEST(NullTerminatedViews, stops_at_null) {
  const char buffer[] = {'a', '\0', 'b', '\0'};
  EXPECT_EQ(NullTerminated::convert_cstring(buffer, sizeof(buffer)), "a"_view);
}

VALIDATION_TEST(NullTerminatedViews, exact_bytes) {
  const char buffer[] = {'a', '\0', 'b', '\0'};
  EXPECT_EQ(NullTerminated::to_view(buffer, sizeof(buffer)), "a\0b\0"_view);
}

VALIDATION_TEST(NullTerminatedViews, caller_bound_exceeds_one_kib) {
  char buffer[1026];
  for (char& byte : buffer) {
    byte = 'x';
  }
  buffer[1025] = '\0';

  auto view = NullTerminated::convert_cstring(buffer, sizeof(buffer));
  EXPECT_EQ(view.get_size(), 1025);
  EXPECT_EQ(view[1024], U8{'x'});
}
