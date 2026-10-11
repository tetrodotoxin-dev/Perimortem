// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.hpp"

#include "perimortem/core/uuid.h"

namespace Perimortem::System {

// Supplies fast thread local pseudorandom values and direct platform seed
// entropy through separate calls. Normal runtime work should use `generate()`,
// which advances one thread local Philox counter. `read_entropy()` crosses into
// the platform entropy source for seeding and values whose unpredictability
// matters more than throughput.
class Random {
 public:
  static EXPORTED(PERIMORTEM) auto generate() -> U64;

  static EXPORTED(PERIMORTEM) auto read_entropy() -> U64;

  // Generates an RFC 4122 version 4 identifier from the thread local stream.
  static EXPORTED(PERIMORTEM) auto generate_uuid_v4() -> Core::Uuid;

  // The timestamp orders version 7 identifiers from different Unix
  // milliseconds while the random suffix distinguishes independent
  // generators. Calls within one millisecond remain randomly ordered, and a
  // clock adjustment applies to the next timestamp.
  static EXPORTED(PERIMORTEM) auto generate_uuid_v7() -> Core::Uuid;
};

}  // namespace Perimortem::System
