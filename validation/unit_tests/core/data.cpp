// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/data.hpp"

#include "toolchain/validation/unit_test.hpp"

using namespace Perimortem::Core;
using namespace Toolchain::Validation;

static constexpr U8 signed_little[] = {0x18, 0xFC};
static constexpr U8 signed_big[] = {0xFC, 0x18};
static constexpr U8 signed_wide_little[] = {
  0x00, 0x36, 0x65, 0xC4, 0xFF, 0xFF, 0xFF, 0xFF,
};
static constexpr U8 signed_wide_big[] = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xC4, 0x65, 0x36, 0x00,
};

static_assert(Data::read<Data::ByteOrder::Little, S8>(signed_little) == S8(24));
static_assert(
    Data::read<Data::ByteOrder::Little, S16>(signed_little) == S16(-1000));
static_assert(Data::read<Data::ByteOrder::Big, S16>(signed_big) == S16(-1000));
static_assert(
    Data::read<Data::ByteOrder::Little, S64>(signed_wide_little) ==
    S64(-1000000000));
static_assert(
    Data::read<Data::ByteOrder::Big, S64>(signed_wide_big) == S64(-1000000000));

static Harness CoreData = {
  .name = "Core::Data",
};

VALIDATION_TEST(CoreData, packed_integers) {
  const U8 byte[] = {0xD6};
  alignas(8) const U8 little_bytes[] = {0, 0x18, 0xFC};
  alignas(8) const U8 big_bytes[] = {0, 0xFF, 0xFE, 0x79, 0x60};
  alignas(8) const U8 wide_bytes[] = {
    0, 0xFF, 0xFF, 0xFF, 0xFF, 0xC4, 0x65, 0x36, 0x00,
  };

  const S8 byte_value = Data::read<Data::ByteOrder::Little, S8>(byte);
  const S16 little = Data::read<Data::ByteOrder::Little, S16>(little_bytes + 1);
  const S32 big = Data::read<Data::ByteOrder::Big, S32>(big_bytes + 1);
  const S64 wide = Data::read<Data::ByteOrder::Big, S64>(wide_bytes + 1);

  EXPECT_EQ(byte_value, S8(-42));
  EXPECT_EQ(little, S16(-1000));
  EXPECT_EQ(big, S32(-100000));
  EXPECT_EQ(wide, S64(-1000000000));
}
