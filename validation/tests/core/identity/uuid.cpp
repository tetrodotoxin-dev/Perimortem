// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/identity/uuid.hpp"

#include "toolchain/validation/test.hpp"

using namespace Perimortem::Core;
using namespace Toolchain::Validation;

static_assert(Identity::Uuid() == Identity::Uuid(0, 0));
static_assert(Identity::Uuid(0, U64(-1)) < Identity::Uuid(1, 0));
static_assert(Identity::Uuid(1, 0) != Identity::Uuid(0, 1));
static_assert([] {
  constexpr Identity::Uuid value{0x0123456789abcdefULL, 0xfedcba9876543210ULL};
  constexpr perimortem_uuid record = value;
  return Identity::Uuid(record) == value;
}());

static Harness CoreUuid = {
  .name = "Core::Identity::Uuid",
};

VALIDATION_TEST(CoreUuid, default) {
  Identity::Uuid uuid{};
  constexpr Identity::Uuid uuid_const{};

  EXPECT(!uuid.is_set());
  EXPECT_EQ(uuid.get_value().high, 0);
  EXPECT_EQ(uuid.get_value().low, 0);

  EXPECT(!uuid_const.is_set());
  EXPECT_EQ(uuid_const.get_value().high, 0);
  EXPECT_EQ(uuid_const.get_value().low, 0);

  EXPECT(Identity::Uuid(1, 0).is_set());
  EXPECT(Identity::Uuid(0, 1).is_set());
}

VALIDATION_TEST(CoreUuid, words) {
  Identity::Uuid uuid{0x0123456789abcdefULL, 0xfedcba9876543210ULL};
  constexpr Identity::Uuid uuid_const{
    0x0123456789abcdefULL, 0xfedcba9876543210ULL};

  EXPECT(uuid.is_set());
  EXPECT_EQ(uuid.get_value().high, 0x0123456789abcdefULL);
  EXPECT_EQ(uuid.get_value().low, 0xfedcba9876543210ULL);

  EXPECT(uuid_const.is_set());
  EXPECT_EQ(uuid_const.get_value().high, 0x0123456789abcdefULL);
  EXPECT_EQ(uuid_const.get_value().low, 0xfedcba9876543210ULL);

  EXPECT(uuid == uuid_const);
}

VALIDATION_TEST(CoreUuid, comparison) {
  const Identity::Uuid zero;
  const Identity::Uuid low{0, U64(-1)};
  const Identity::Uuid high{1, 0};
  const Identity::Uuid maximum{U64(-1), U64(-1)};

  EXPECT(zero == Identity::Uuid(0, 0));
  EXPECT(zero < low);
  EXPECT(low < high);
  EXPECT(high < maximum);
  EXPECT_NOT(high < low);
  EXPECT_NOT(maximum < maximum);
  EXPECT(Identity::Uuid(1, 2) != Identity::Uuid(1, 3));
  EXPECT(Identity::Uuid(1, 2) != Identity::Uuid(2, 2));
}

VALIDATION_TEST(CoreUuid, c_record_copy) {
  perimortem_uuid source{U64(-1), U64(-1)};
  const Identity::Uuid uuid(source);
  source.high = 0;
  source.low = 0;

  const perimortem_uuid record = uuid;
  EXPECT_EQ(record.high, U64(-1));
  EXPECT_EQ(record.low, U64(-1));
  EXPECT(uuid == Identity::Uuid(record));
  EXPECT(uuid != Identity::Uuid(source));
}
