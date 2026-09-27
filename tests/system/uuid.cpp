// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/uuid.hpp"

#include "toolchain/validation/unit_test.hpp"

#include "perimortem/core/null_terminated.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::System;

using namespace Toolchain::Validation;

static Harness SystemUuid = {
  .name = "System::Uuid",
};

VALIDATION_TEST(SystemUuid, default) {
  Uuid uuid;
  constexpr Uuid uuid_const;

  EXPECT(!uuid.is_set());
  EXPECT_EQ(uuid.get_value().high, 0);
  EXPECT_EQ(uuid.get_value().low, 0);

  EXPECT(!uuid_const.is_set());
  EXPECT_EQ(uuid_const.get_value().high, 0);
  EXPECT_EQ(uuid_const.get_value().low, 0);

  EXPECT(Uuid(1, 0).is_set());
  EXPECT(Uuid(0, 1).is_set());
}

VALIDATION_TEST(SystemUuid, deserialize_bits_64) {
  Uuid uuid{0x0123456789abcdefULL, 0xfedcba9876543210ULL};
  constexpr Uuid uuid_const{0x0123456789abcdefULL, 0xfedcba9876543210ULL};

  EXPECT(uuid.is_set());
  EXPECT_EQ(uuid.get_value().high, 0x0123456789abcdefULL);
  EXPECT_EQ(uuid.get_value().low, 0xfedcba9876543210ULL);

  EXPECT(uuid_const.is_set());
  EXPECT_EQ(uuid_const.get_value().high, 0x0123456789abcdefULL);
  EXPECT_EQ(uuid_const.get_value().low, 0xfedcba9876543210ULL);

  EXPECT(uuid == uuid_const);
}

VALIDATION_TEST(SystemUuid, decode_32_nibble) {
  Uuid uuid("0123456789abcdeffedcba9876543210"_bytes);
  constexpr Uuid uuid_const("0123456789abcdeffedcba9876543210"_bytes);

  EXPECT(uuid.is_set());
  EXPECT_EQ(uuid.get_value().high, 0x0123456789abcdefULL);
  EXPECT_EQ(uuid.get_value().low, 0xfedcba9876543210ULL);

  EXPECT(uuid_const.is_set());
  EXPECT_EQ(uuid_const.get_value().high, 0x0123456789abcdefULL);
  EXPECT_EQ(uuid_const.get_value().low, 0xfedcba9876543210ULL);

  EXPECT(uuid == uuid_const);
}

VALIDATION_TEST(SystemUuid, deserialize_uuid_v4) {
  Uuid uuid("cb8c03d8-2b01-4a39-b31d-2c6805e6503c"_bytes);
  constexpr Uuid uuid_const("cb8c03d8-2b01-4a39-b31d-2c6805e6503c"_bytes);

  EXPECT(uuid.is_set());
  EXPECT_EQ(uuid.get_value().high, 0xcb8c03d82b014a39ULL);
  EXPECT_EQ(uuid.get_value().low, 0xb31d2c6805e6503cULL);

  EXPECT(uuid_const.is_set());
  EXPECT_EQ(uuid_const.get_value().high, 0xcb8c03d82b014a39ULL);
  EXPECT_EQ(uuid_const.get_value().low, 0xb31d2c6805e6503cULL);

  EXPECT(uuid == uuid_const);
}

VALIDATION_TEST(SystemUuid, serialize_uuid_v4) {
  Uuid uuid(0xcb8c03d82b014a39ULL, 0xb31d2c6805e6503cULL);

  EXPECT_TEXT(uuid.serialize(), "cb8c03d8-2b01-4a39-b31d-2c6805e6503c"_bytes);
}

VALIDATION_TEST(SystemUuid, generate_v4) {
  Uuid uuid = Uuid::generate_v4();

  EXPECT(uuid.is_set());

  const auto serialized = uuid.serialize();
  EXPECT_EQ(serialized[14], U8('4'));
  EXPECT(
      serialized[19] == '8' || serialized[19] == '9' || serialized[19] == 'a' ||
      serialized[19] == 'b');
}

VALIDATION_TEST(SystemUuid, generate_v7) {
  Uuid uuid = Uuid::generate_v7();

  auto output = uuid.serialize();
  EXPECT(output.hash());
  EXPECT_EQ(output[14], U8('7'));
  EXPECT(
      output[19] == '8' || output[19] == '9' || output[19] == 'a' ||
      output[19] == 'b');
  EXPECT(uuid.is_set());
}
