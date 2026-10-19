// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "validation/tests/core/abi.h"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/access/bytes.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/core/view/vector.hpp"
#include "perimortem/serialization/formats/uuid.hpp"

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
static_assert(!__is_constructible(View::Bytes, const U8*, Count));
static_assert(!__is_constructible(View::Bytes, U8&&, Count));
static_assert(!__is_constructible(View::Bytes, U8 (&&)[3]));
static_assert(!__is_constructible(View::Vector<U8>, const U8*, Count));
static_assert(!__is_constructible(View::Vector<U8>, U8&&, Count));
static_assert(!__is_constructible(View::Vector<U8>, U8 (&&)[3]));
static_assert(sizeof(Access::Bytes) == sizeof(perimortem_access_bytes));
static_assert(alignof(Access::Bytes) == alignof(perimortem_access_bytes));
static_assert(!__is_constructible(Access::Bytes));
static_assert(!__is_constructible(Access::Bytes, U8*, Count));
static_assert(!__is_constructible(Access::Bytes, U8&&, Count));
static_assert(!__is_constructible(Access::Bytes, U8 (&&)[3]));
static_assert(sizeof(Formats::Uuid) == sizeof(perimortem_uuid));
static_assert(alignof(Formats::Uuid) == alignof(perimortem_uuid));
static_assert([] {
  View::Bytes view;
  Formats::Uuid uuid;
  return view.is_empty() && !uuid.is_set();
}());

static_assert([] {
  U8 data[] = {1, 2, 3};
  View::Bytes view{data};
  return view.get_size() == 3 && view[1] == 2 && view[3] == 0;
}());

static_assert([] {
  View::Bytes invalid(perimortem_view_bytes{nullptr, 3});
  return invalid.is_empty() && invalid.get_data() == nullptr;
}());

static_assert([] {
  U32 data[] = {1, 2, 3};
  View::Vector<U32> view(data);
  return view.get_size() == 3 && view[1] == 2 && view[3] == 0;
}());

static_assert([] {
  U8 data[] = {1, 2, 3};
  Access::Bytes access(data);
  auto selected = access[1];
  if (!selected) {
    return false;
  }

  selected.visit([](U8& value) { value = 4; }, []() {});
  return data[1] == 4;
}());

static_assert(
    Formats::Uuid::deserialize("0123456789abcdeffedcba9876543210"_bytes)
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

  auto access = Access::Bytes::create(perimortem_test_access(data, 3));
  ASSERT(access);
  access.visit(
      [](Access::Bytes& bytes) {
        bytes[1].visit([](U8& value) { value = 4; }, []() {});
      },

      []() {});
  EXPECT_EQ(data[1], 4);

  EXPECT_NOT(Access::Bytes::create({nullptr, 0}));
  EXPECT_NOT(Access::Bytes::create({nullptr, 3}));

  Formats::Uuid uuid = perimortem_test_uuid(5, 6);
  EXPECT_EQ(uuid.get_value().high, U64(5));
  EXPECT_EQ(uuid.get_value().low, U64(6));
}
