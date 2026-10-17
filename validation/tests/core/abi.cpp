// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "validation/tests/core/abi.h"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/view/null_terminated.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Serialization;
using namespace Toolchain::Validation;

// The public C records remain plain values that C++ can copy without invoking
// language specific lifetime behavior.
static_assert(__is_standard_layout(perimortem_view_bytes));
static_assert(__is_trivially_copyable(perimortem_view_bytes));
static_assert(__is_trivial(perimortem_view_bytes));
static_assert(__is_aggregate(perimortem_view_bytes));
static_assert(__is_standard_layout(perimortem_access_bytes));
static_assert(__is_trivially_copyable(perimortem_access_bytes));
static_assert(__is_trivial(perimortem_access_bytes));
static_assert(__is_aggregate(perimortem_access_bytes));
static_assert(__is_standard_layout(perimortem_uuid));
static_assert(__is_trivially_copyable(perimortem_uuid));
static_assert(__is_trivial(perimortem_uuid));
static_assert(__is_aggregate(perimortem_uuid));

static_assert(sizeof(View::Bytes) == sizeof(perimortem_view_bytes));
static_assert(alignof(View::Bytes) == alignof(perimortem_view_bytes));
static_assert(sizeof(Access::Bytes) == sizeof(perimortem_access_bytes));
static_assert(alignof(Access::Bytes) == alignof(perimortem_access_bytes));
static_assert(sizeof(Uuid) == sizeof(perimortem_uuid));
static_assert(alignof(Uuid) == alignof(perimortem_uuid));
static_assert([] {
  View::Bytes view;
  Access::Bytes access;
  Uuid uuid;
  return view.is_empty() && access.is_empty() && !uuid.is_set();
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

  selected.visit([](U8& value) { value = 4; }, []() {});
  return data[1] == 4;
}());

static_assert(
    Uuid::deserialize("0123456789abcdeffedcba9876543210"_bytes)
        .get_value()
        .high == 0x0123456789abcdefULL);

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
  selected.visit([](U8& value) { value = 4; }, []() {});
  EXPECT_EQ(data[1], 4);

  Uuid uuid = perimortem_test_uuid(5, 6);
  EXPECT_EQ(uuid.get_value().high, U64(5));
  EXPECT_EQ(uuid.get_value().low, U64(6));
}
