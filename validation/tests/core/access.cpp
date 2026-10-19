// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/core/access/bytes.hpp"
#include "perimortem/core/access/vector.hpp"
#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/core/storage/vector.hpp"

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

  S32 empty_value = 0;
  Access::Vector<S32> empty(empty_value, 0);
  EXPECT(empty.get_data() == &empty_value);
  EXPECT_NOT(empty[0]);

  EXPECT_NOT(Access::Vector<S32>::create(nullptr, 0));
  EXPECT_NOT(Access::Vector<S32>::create(nullptr, 3));
}

VALIDATION_TEST(CoreAccess, bounds_bytes) {
  U8 values[3] = {1, 2, 3};
  Access::Bytes access(values);
  auto selected = access[2];
  ASSERT(selected);
  selected.visit([](U8& value) { value = 4; }, []() {});
  EXPECT_EQ(values[2], 4);

  EXPECT_NOT(access[3]);
  EXPECT_NOT(access[Count(-1)]);

  U8 empty_value = 0;
  Access::Bytes empty(empty_value, 0);
  EXPECT(empty.get_data() == &empty_value);
  EXPECT_NOT(empty[0]);

  EXPECT_NOT(Access::Bytes::create({nullptr, 0}));
  EXPECT_NOT(Access::Bytes::create({nullptr, 3}));
}

VALIDATION_TEST(CoreAccess, embedded_empty_storage) {
  Storage::Bytes<0> bytes;
  auto byte_access = bytes.get_access();
  EXPECT(byte_access.is_empty());
  EXPECT(byte_access.get_data() != nullptr);

  Storage::Vector<U32, 0> values;
  auto value_access = values.get_access();
  EXPECT(value_access.is_empty());
  EXPECT(value_access.get_data() != nullptr);
}
