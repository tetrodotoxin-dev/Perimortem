// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/random.hpp"

#include "toolchain/validation/test.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::System;
using namespace Toolchain::Validation;

static Harness SystemRandom = {
  .name = "System::Random",
};

VALIDATION_TEST(SystemRandom, generate_uuid_v4) {
  Uuid uuid = Random::generate_uuid_v4();

  EXPECT(uuid.is_set());

  const auto serialized = uuid.serialize();
  EXPECT_EQ(serialized[14], U8('4'));
  EXPECT(
      serialized[19] == '8' || serialized[19] == '9' || serialized[19] == 'a' ||
      serialized[19] == 'b');
}

VALIDATION_TEST(SystemRandom, generate_uuid_v7) {
  Uuid uuid = Random::generate_uuid_v7();

  auto output = uuid.serialize();
  EXPECT(output.hash());
  EXPECT_EQ(output[14], U8('7'));
  EXPECT(
      output[19] == '8' || output[19] == '9' || output[19] == 'a' ||
      output[19] == 'b');
  EXPECT(uuid.is_set());
}
