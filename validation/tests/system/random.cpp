// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/random.hpp"

#include <time.h>

#include "toolchain/validation/test.hpp"

#include "perimortem/core/view/null_terminated.hpp"
#include "validation/tests/log.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::System;
using namespace Perimortem::Tests;
using namespace Toolchain::Validation;

static Harness SystemRandom = {
  .name = "System::Random",
  .setup = Log::begin,
  .teardown = Log::end,
};

VALIDATION_TEST(SystemRandom, generate_stream) {
  const U64 first = Random::generate();
  Bool changed = False;
  for (Count index = 0; index < 64; index++) {
    changed |= Random::generate() != first;
  }

  EXPECT(changed);

  const View::Bytes diagnostic = Log::captured_message();
  EXPECT(Log::captured_count() <= Count(1));
  EXPECT(
      diagnostic.is_empty() ||
      Log::warning_contains("System::Random seed "_view));
}

VALIDATION_TEST(SystemRandom, generate_uuid_v4) {
  const Identity::Uuid uuid = Random::generate_uuid_v4();
  const auto value = uuid.get_value();

  EXPECT(uuid.is_set());

  EXPECT_EQ((value.high >> 12) & 15, U64(4));
  EXPECT_EQ(value.low >> 62, U64(2));
}

VALIDATION_TEST(SystemRandom, generate_uuid_v7) {
  const U64 before = U64(time(nullptr)) * 1'000;
  const Identity::Uuid uuid = Random::generate_uuid_v7();
  const U64 after = U64(time(nullptr) + 1) * 1'000;

  const auto value = uuid.get_value();
  const U64 timestamp = value.high >> 16;
  EXPECT(timestamp >= before);
  EXPECT(timestamp < after);
  EXPECT_EQ((value.high >> 12) & 15, U64(7));
  EXPECT_EQ(value.low >> 62, U64(2));
  EXPECT(uuid.is_set());
}
