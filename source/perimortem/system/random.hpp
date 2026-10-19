// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.hpp"

#include "perimortem/serialization/formats/uuid.hpp"

namespace Perimortem::System {

// Random provides one fast Philox stream for each thread. The stream supports
// simulations, identifiers and other work that values throughput over
// cryptographic unpredictability. Native threads prefer RDRAND and fall back to
// host entropy where a direct source is available. Every seed also mixes high
// precision clock samples, local addresses and a process sequence. A process
// emits at most one warning for each fallback source so the seed source remains
// observable without repeating the same diagnostic for every thread. Generated
// values remain unsuitable for secrets.
class Random {
 public:
  static EXPORTED(PERIMORTEM) auto generate() -> U64;

  // Generates an RFC 4122 version 4 identifier from the noncryptographic thread
  // local stream. Use it for identity rather than secrecy.
  static EXPORTED(PERIMORTEM) auto generate_uuid_v4()
      -> Serialization::Formats::Uuid;

  // The timestamp orders version 7 identifiers from different Unix
  // milliseconds while the random suffix distinguishes independent
  // generators. Calls within one millisecond remain randomly ordered, and a
  // clock adjustment applies to the next timestamp.
  static EXPORTED(PERIMORTEM) auto generate_uuid_v7()
      -> Serialization::Formats::Uuid;
};

}  // namespace Perimortem::System
