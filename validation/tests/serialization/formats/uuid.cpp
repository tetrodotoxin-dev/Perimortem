// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/serialization/formats/uuid.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/view/null_terminated.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Serialization;

using namespace Toolchain::Validation;

static Harness SerializationUuid = {
  .name = "Serialization::Formats::Uuid",
};

static_assert(Formats::Uuid::is_valid("0123456789abcdeffedcba9876543210"_view));
static_assert(
    Formats::Uuid::is_valid("CB8C03D8-2B01-4A39-B31D-2C6805E6503C"_view));
static_assert(
    !Formats::Uuid::is_valid("cb8c03d8-2b01-4a39-b31d-2c6805e6503g"_view));
static_assert(
    !Formats::Uuid::deserialize("cb8c03d8-2b01-4a39-b31d-2c6805e6503g"_bytes)
         .is_set());

VALIDATION_TEST(SerializationUuid, default) {
  Formats::Uuid uuid{};
  constexpr Formats::Uuid uuid_const{};

  EXPECT(!uuid.is_set());
  EXPECT_EQ(uuid.get_value().high, 0);
  EXPECT_EQ(uuid.get_value().low, 0);

  EXPECT(!uuid_const.is_set());
  EXPECT_EQ(uuid_const.get_value().high, 0);
  EXPECT_EQ(uuid_const.get_value().low, 0);

  EXPECT(Formats::Uuid(1, 0).is_set());
  EXPECT(Formats::Uuid(0, 1).is_set());
}

VALIDATION_TEST(SerializationUuid, deserialize_bits_64) {
  Formats::Uuid uuid{0x0123456789abcdefULL, 0xfedcba9876543210ULL};
  constexpr Formats::Uuid uuid_const{
    0x0123456789abcdefULL, 0xfedcba9876543210ULL};

  EXPECT(uuid.is_set());
  EXPECT_EQ(uuid.get_value().high, 0x0123456789abcdefULL);
  EXPECT_EQ(uuid.get_value().low, 0xfedcba9876543210ULL);

  EXPECT(uuid_const.is_set());
  EXPECT_EQ(uuid_const.get_value().high, 0x0123456789abcdefULL);
  EXPECT_EQ(uuid_const.get_value().low, 0xfedcba9876543210ULL);

  EXPECT(uuid == uuid_const);
}

VALIDATION_TEST(SerializationUuid, decode_32_nibble) {
  Formats::Uuid uuid =
      Formats::Uuid::deserialize("0123456789abcdeffedcba9876543210"_bytes);
  constexpr Formats::Uuid uuid_const =
      Formats::Uuid::deserialize("0123456789abcdeffedcba9876543210"_bytes);

  EXPECT(uuid.is_set());
  EXPECT_EQ(uuid.get_value().high, 0x0123456789abcdefULL);
  EXPECT_EQ(uuid.get_value().low, 0xfedcba9876543210ULL);

  EXPECT(uuid_const.is_set());
  EXPECT_EQ(uuid_const.get_value().high, 0x0123456789abcdefULL);
  EXPECT_EQ(uuid_const.get_value().low, 0xfedcba9876543210ULL);

  EXPECT(uuid == uuid_const);
}

VALIDATION_TEST(SerializationUuid, deserialize_uuid_v4) {
  Formats::Uuid uuid =
      Formats::Uuid::deserialize("cb8c03d8-2b01-4a39-b31d-2c6805e6503c"_bytes);
  constexpr Formats::Uuid uuid_const =
      Formats::Uuid::deserialize("cb8c03d8-2b01-4a39-b31d-2c6805e6503c"_bytes);

  EXPECT(uuid.is_set());
  EXPECT_EQ(uuid.get_value().high, 0xcb8c03d82b014a39ULL);
  EXPECT_EQ(uuid.get_value().low, 0xb31d2c6805e6503cULL);

  EXPECT(uuid_const.is_set());
  EXPECT_EQ(uuid_const.get_value().high, 0xcb8c03d82b014a39ULL);
  EXPECT_EQ(uuid_const.get_value().low, 0xb31d2c6805e6503cULL);

  EXPECT(uuid == uuid_const);
}

VALIDATION_TEST(SerializationUuid, uppercase_hexadecimal) {
  Formats::Uuid packed =
      Formats::Uuid::deserialize("0123456789ABCDEFFEDCBA9876543210"_bytes);
  constexpr Formats::Uuid packed_const =
      Formats::Uuid::deserialize("0123456789ABCDEFFEDCBA9876543210"_bytes);
  Formats::Uuid dashed =
      Formats::Uuid::deserialize("CB8C03D8-2B01-4A39-B31D-2C6805E6503C"_bytes);
  constexpr Formats::Uuid dashed_const =
      Formats::Uuid::deserialize("CB8C03D8-2B01-4A39-B31D-2C6805E6503C"_bytes);

  EXPECT_EQ(packed.get_value().high, 0x0123456789abcdefULL);
  EXPECT_EQ(packed.get_value().low, 0xfedcba9876543210ULL);
  EXPECT(packed == packed_const);

  EXPECT_EQ(dashed.get_value().high, 0xcb8c03d82b014a39ULL);
  EXPECT_EQ(dashed.get_value().low, 0xb31d2c6805e6503cULL);
  EXPECT(dashed == dashed_const);
}

VALIDATION_TEST(SerializationUuid, validation) {
  EXPECT(Formats::Uuid::is_valid("00000000000000000000000000000000"_view));
  EXPECT(Formats::Uuid::is_valid("0123456789abcdeffedcba9876543210"_view));
  EXPECT(Formats::Uuid::is_valid("0123456789ABCDEFFEDCBA9876543210"_view));
  EXPECT(Formats::Uuid::is_valid("cb8c03d8-2b01-4a39-b31d-2c6805e6503c"_view));
  EXPECT(Formats::Uuid::is_valid("CB8C03D8-2B01-4A39-B31D-2C6805E6503C"_view));

  EXPECT(!Formats::Uuid::is_valid(""_view));
  EXPECT(!Formats::Uuid::is_valid("0123456789abcdef"_view));
  EXPECT(!Formats::Uuid::is_valid("0123456789abcdeffedcba987654321/"_view));
  EXPECT(!Formats::Uuid::is_valid("0123456789abcdeffedcba987654321:"_view));
  EXPECT(!Formats::Uuid::is_valid("0123456789abcdeffedcba987654321@"_view));
  EXPECT(!Formats::Uuid::is_valid("0123456789abcdeffedcba987654321G"_view));
  EXPECT(!Formats::Uuid::is_valid("0123456789abcdeffedcba987654321`"_view));
  EXPECT(!Formats::Uuid::is_valid("0123456789abcdeffedcba987654321g"_view));
  EXPECT(!Formats::Uuid::is_valid("cb8c03d82b01-4a39-b31d-2c6805e6503c"_view));
  EXPECT(!Formats::Uuid::is_valid("cb8c03d8-2b01-4a39-b31d_2c6805e6503c"_view));

  EXPECT(!Formats::Uuid::deserialize("00000000000000000000000000000000"_bytes)
              .is_set());
  EXPECT(!Formats::Uuid::deserialize("0123456789abcdeffedcba987654321g"_bytes)
              .is_set());
  EXPECT(
      !Formats::Uuid::deserialize("cb8c03d8-2b01-4a39-b31d_2c6805e6503c"_bytes)
           .is_set());
}

VALIDATION_TEST(SerializationUuid, serialize_uuid_v4) {
  Formats::Uuid uuid{0xcb8c03d82b014a39ULL, 0xb31d2c6805e6503cULL};

  EXPECT_TEXT(uuid.serialize(), "cb8c03d8-2b01-4a39-b31d-2c6805e6503c"_bytes);
}
