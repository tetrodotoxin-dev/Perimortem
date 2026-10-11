// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/uuid.h"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/null_terminated.hpp"

using namespace Perimortem::Core;

using namespace Toolchain::Validation;

static Harness CoreUuid = {
  .name = "Core::Uuid",
};

static_assert(Uuid::is_valid("0123456789abcdeffedcba9876543210"_view));
static_assert(Uuid::is_valid("CB8C03D8-2B01-4A39-B31D-2C6805E6503C"_view));
static_assert(!Uuid::is_valid("cb8c03d8-2b01-4a39-b31d-2c6805e6503g"_view));
static_assert(
    !Uuid::deserialize("cb8c03d8-2b01-4a39-b31d-2c6805e6503g"_bytes).is_set());

VALIDATION_TEST(CoreUuid, default) {
  Uuid uuid{};
  constexpr Uuid uuid_const{};

  EXPECT(!uuid.is_set());
  EXPECT_EQ(uuid.get_value().high, 0);
  EXPECT_EQ(uuid.get_value().low, 0);

  EXPECT(!uuid_const.is_set());
  EXPECT_EQ(uuid_const.get_value().high, 0);
  EXPECT_EQ(uuid_const.get_value().low, 0);

  EXPECT(Uuid(1, 0).is_set());
  EXPECT(Uuid(0, 1).is_set());
}

VALIDATION_TEST(CoreUuid, deserialize_bits_64) {
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

VALIDATION_TEST(CoreUuid, decode_32_nibble) {
  Uuid uuid = Uuid::deserialize("0123456789abcdeffedcba9876543210"_bytes);
  constexpr Uuid uuid_const =
      Uuid::deserialize("0123456789abcdeffedcba9876543210"_bytes);

  EXPECT(uuid.is_set());
  EXPECT_EQ(uuid.get_value().high, 0x0123456789abcdefULL);
  EXPECT_EQ(uuid.get_value().low, 0xfedcba9876543210ULL);

  EXPECT(uuid_const.is_set());
  EXPECT_EQ(uuid_const.get_value().high, 0x0123456789abcdefULL);
  EXPECT_EQ(uuid_const.get_value().low, 0xfedcba9876543210ULL);

  EXPECT(uuid == uuid_const);
}

VALIDATION_TEST(CoreUuid, deserialize_uuid_v4) {
  Uuid uuid = Uuid::deserialize("cb8c03d8-2b01-4a39-b31d-2c6805e6503c"_bytes);
  constexpr Uuid uuid_const =
      Uuid::deserialize("cb8c03d8-2b01-4a39-b31d-2c6805e6503c"_bytes);

  EXPECT(uuid.is_set());
  EXPECT_EQ(uuid.get_value().high, 0xcb8c03d82b014a39ULL);
  EXPECT_EQ(uuid.get_value().low, 0xb31d2c6805e6503cULL);

  EXPECT(uuid_const.is_set());
  EXPECT_EQ(uuid_const.get_value().high, 0xcb8c03d82b014a39ULL);
  EXPECT_EQ(uuid_const.get_value().low, 0xb31d2c6805e6503cULL);

  EXPECT(uuid == uuid_const);
}

VALIDATION_TEST(CoreUuid, uppercase_hexadecimal) {
  Uuid packed = Uuid::deserialize("0123456789ABCDEFFEDCBA9876543210"_bytes);
  constexpr Uuid packed_const =
      Uuid::deserialize("0123456789ABCDEFFEDCBA9876543210"_bytes);
  Uuid dashed = Uuid::deserialize("CB8C03D8-2B01-4A39-B31D-2C6805E6503C"_bytes);
  constexpr Uuid dashed_const =
      Uuid::deserialize("CB8C03D8-2B01-4A39-B31D-2C6805E6503C"_bytes);

  EXPECT_EQ(packed.get_value().high, 0x0123456789abcdefULL);
  EXPECT_EQ(packed.get_value().low, 0xfedcba9876543210ULL);
  EXPECT(packed == packed_const);

  EXPECT_EQ(dashed.get_value().high, 0xcb8c03d82b014a39ULL);
  EXPECT_EQ(dashed.get_value().low, 0xb31d2c6805e6503cULL);
  EXPECT(dashed == dashed_const);
}

VALIDATION_TEST(CoreUuid, validation) {
  EXPECT(Uuid::is_valid("00000000000000000000000000000000"_view));
  EXPECT(Uuid::is_valid("0123456789abcdeffedcba9876543210"_view));
  EXPECT(Uuid::is_valid("0123456789ABCDEFFEDCBA9876543210"_view));
  EXPECT(Uuid::is_valid("cb8c03d8-2b01-4a39-b31d-2c6805e6503c"_view));
  EXPECT(Uuid::is_valid("CB8C03D8-2B01-4A39-B31D-2C6805E6503C"_view));

  EXPECT(!Uuid::is_valid(""_view));
  EXPECT(!Uuid::is_valid("0123456789abcdef"_view));
  EXPECT(!Uuid::is_valid("0123456789abcdeffedcba987654321/"_view));
  EXPECT(!Uuid::is_valid("0123456789abcdeffedcba987654321:"_view));
  EXPECT(!Uuid::is_valid("0123456789abcdeffedcba987654321@"_view));
  EXPECT(!Uuid::is_valid("0123456789abcdeffedcba987654321G"_view));
  EXPECT(!Uuid::is_valid("0123456789abcdeffedcba987654321`"_view));
  EXPECT(!Uuid::is_valid("0123456789abcdeffedcba987654321g"_view));
  EXPECT(!Uuid::is_valid("cb8c03d82b01-4a39-b31d-2c6805e6503c"_view));
  EXPECT(!Uuid::is_valid("cb8c03d8-2b01-4a39-b31d_2c6805e6503c"_view));

  EXPECT(!Uuid::deserialize("00000000000000000000000000000000"_bytes).is_set());
  EXPECT(!Uuid::deserialize("0123456789abcdeffedcba987654321g"_bytes).is_set());
  EXPECT(!Uuid::deserialize("cb8c03d8-2b01-4a39-b31d_2c6805e6503c"_bytes)
              .is_set());
}

VALIDATION_TEST(CoreUuid, serialize_uuid_v4) {
  Uuid uuid{0xcb8c03d82b014a39ULL, 0xb31d2c6805e6503cULL};

  EXPECT_TEXT(uuid.serialize(), "cb8c03d8-2b01-4a39-b31d-2c6805e6503c"_bytes);
}
