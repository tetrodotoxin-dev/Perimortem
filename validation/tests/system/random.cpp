// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/random.hpp"

#include <time.h>

#include "toolchain/validation/test.hpp"

#include "perimortem/core/view/null_terminated.hpp"
#include "validation/tests/log.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Serialization;
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
  Formats::Uuid uuid = Random::generate_uuid_v4();

  EXPECT(uuid.is_set());

  const auto serialized = uuid.serialize();
  EXPECT_EQ(serialized[14], U8('4'));
  EXPECT(
      serialized[19] == '8' || serialized[19] == '9' || serialized[19] == 'a' ||
      serialized[19] == 'b');
}

VALIDATION_TEST(SystemRandom, generate_uuid_v7) {
  const U64 before = U64(time(nullptr)) * 1'000;
  Formats::Uuid uuid = Random::generate_uuid_v7();
  const U64 after = U64(time(nullptr) + 1) * 1'000;

  auto output = uuid.serialize();
  const U64 timestamp = uuid.get_value().high >> 16;
  EXPECT(timestamp >= before);
  EXPECT(timestamp < after);
  EXPECT(output.hash());
  EXPECT_EQ(output[14], U8('7'));
  EXPECT(
      output[19] == '8' || output[19] == '9' || output[19] == 'a' ||
      output[19] == 'b');
  EXPECT(uuid.is_set());
}
