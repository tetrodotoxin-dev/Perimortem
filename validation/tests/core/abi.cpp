// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "validation/tests/core/abi.h"

#include "toolchain/validation/test.hpp"

using namespace Perimortem::Core;
using namespace Toolchain::Validation;

static_assert(sizeof(View::Bytes) == sizeof(perimortem_view_bytes));
static_assert(alignof(View::Bytes) == alignof(perimortem_view_bytes));
static_assert(sizeof(Access::Bytes) == sizeof(perimortem_access_bytes));
static_assert(alignof(Access::Bytes) == alignof(perimortem_access_bytes));
static_assert([] {
  View::Bytes view;
  Access::Bytes access;
  return view.is_empty() && access.is_empty();
}());
static_assert([] {
  U8 data[] = {1, 2, 3};
  View::Bytes view{data, 3};
  return view.get_size() == 3 && view[1] == 2;
}());
static_assert([] {
  U8 data[] = {1, 2, 3};
  Access::Bytes access{data, 3};
  auto selected = access[1];
  if (!selected) {
    return false;
  }

  *selected = 4;
  return data[1] == 4;
}());

static Harness CoreAbi = {
  .name = "Core::ABI",
};

VALIDATION_TEST(CoreAbi, c_records) {
  U8 data[] = {1, 2, 3};

  View::Bytes view(perimortem_test_view(data, 3));
  EXPECT_EQ(view.get_data(), data);
  EXPECT_EQ(view.get_size(), Count(3));

  Access::Bytes access(perimortem_test_access(data, 3));
  auto selected = access[1];
  ASSERT(selected);
  *selected = 4;
  EXPECT_EQ(data[1], 4);
}
