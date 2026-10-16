// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/core/access/bytes.h"
#include "perimortem/core/access/vector.hpp"

using namespace Perimortem::Core;
using namespace Toolchain::Validation;

static Harness CoreAccess = {.name = "Core::Access"};

VALIDATION_TEST(CoreAccess, bounds_vector) {
  S32 values[3] = {1, 2, 3};
  Access::Vector<S32> access(values);
  auto selected = access[2];
  ASSERT(selected);
  selected.visit([](S32& value) { value = 4; }, []() {});
  EXPECT_EQ(values[2], 4);

  EXPECT_NOT(access[3]);
  EXPECT_NOT(access[Count(-1)]);

  Access::Vector<S32> empty;
  EXPECT_NOT(empty[0]);

  Access::Vector<S32> invalid(nullptr, 3);
  EXPECT_EQ(invalid.get_size(), 0);
  EXPECT_NOT(invalid[0]);
}

VALIDATION_TEST(CoreAccess, bounds_bytes) {
  U8 values[3] = {1, 2, 3};
  Access::Bytes access{values, 3};
  auto selected = access[2];
  ASSERT(selected);
  selected.visit([](U8& value) { value = 4; }, []() {});
  EXPECT_EQ(values[2], 4);

  EXPECT_NOT(access[3]);
  EXPECT_NOT(access[Count(-1)]);

  Access::Bytes empty{};
  EXPECT_NOT(empty[0]);

  Access::Bytes invalid(nullptr, 3);
  EXPECT_EQ(invalid.get_size(), 0);
  EXPECT_NOT(invalid[0]);
}
