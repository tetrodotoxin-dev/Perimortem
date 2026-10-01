// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/access/bytes.hpp"
#include "perimortem/core/access/vector.hpp"

#include "toolchain/validation/unit_test.hpp"

using namespace Perimortem::Core;
using namespace Toolchain::Validation;

static Harness CoreAccess = {.name = "Core::Access"};

VALIDATION_TEST(CoreAccess, saturates_vector) {
  S32 values[3] = {1, 2, 3};
  Access::Vector<S32> access(values);
  auto selected = access[99];
  ASSERT(selected);
  *selected = 4;
  EXPECT_EQ(values[2], 4);
  auto first = access[-1];
  ASSERT(first);
  EXPECT_EQ(*first, 1);
  Access::Vector<S32> empty;
  EXPECT_NOT(empty[0]);
  Access::Vector<S32> invalid(nullptr, 3);
  EXPECT_EQ(invalid.get_size(), 0);
  EXPECT_NOT(invalid[0]);
}

VALIDATION_TEST(CoreAccess, saturates_bytes) {
  U8 values[3] = {1, 2, 3};
  Access::Bytes access(values);
  auto selected = access[99];
  ASSERT(selected);
  *selected = 4;
  EXPECT_EQ(values[2], 4);
  auto first = access[-1];
  ASSERT(first);
  EXPECT_EQ(*first, 1);
  Access::Bytes empty;
  EXPECT_NOT(empty[0]);
  Access::Bytes invalid(nullptr, 3);
  EXPECT_EQ(invalid.get_size(), 0);
  EXPECT_NOT(invalid[0]);
}
