// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/serialization/uuid.h"

#include "toolchain/toolchain.hpp"
#include "toolchain/validation/benchmark.hpp"

#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/system/random.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Serialization;
using namespace Perimortem::System;
using namespace Toolchain::Validation;

static constexpr Count uuid_batch = 1024;
static constexpr Uuid serialize_source_a{
  0x550e8400e29b41d4ULL, 0xa716446655440000ULL};

static constexpr Uuid serialize_source_b{
  0x123e4567e89b42d3ULL, 0xa456426614174000ULL};

static constexpr auto dashed_source_a =
    "550e8400-e29b-41d4-a716-446655440000"_bytes;
static constexpr auto dashed_source_b =
    "123e4567-e89b-42d3-a456-426614174000"_bytes;
static constexpr auto packed_source_a =
    "550e8400e29b41d4a716446655440000"_bytes;
static constexpr auto packed_source_b =
    "123e4567e89b42d3a456426614174000"_bytes;
static U64 uuid_source_offset = 0;

static auto select_uuid_source() -> void {
  uuid_source_offset = Random::generate();
}

static Harness SerializationUuid = {
  .name = "UUIDs",
  .setup = select_uuid_source,
};

VALIDATION_BENCHMARK(SerializationUuid, serialize_x1024) {
  // Measures formatting from an existing UUID so generation remains outside
  // the measurement.
  Count value = 0;
  for (Count i = 0; i < uuid_batch; i++) {
    const auto* source = ((U64(i) + uuid_source_offset) & 1) == 0
                             ? &serialize_source_a
                             : &serialize_source_b;
    Benchmark::prevent_optimization(source);

    auto serialized = source->serialize();
    value += serialized[i % serialized.get_size()];
  }

  Benchmark::prevent_optimization(value);
}

VALIDATION_BENCHMARK(SerializationUuid, deserialize_dashed_x1024) {
  Count value = 0;
  for (Count i = 0; i < uuid_batch; i++) {
    const auto* source = ((U64(i) + uuid_source_offset) & 1) == 0
                             ? &dashed_source_a
                             : &dashed_source_b;
    Benchmark::prevent_optimization(source);

    const Uuid uuid = Uuid::deserialize(*source);
    value ^= uuid.get_value().high;
  }

  Benchmark::prevent_optimization(value);
}

VALIDATION_BENCHMARK(SerializationUuid, deserialize_packed_x1024) {
  Count value = 0;
  for (Count i = 0; i < uuid_batch; i++) {
    const auto* source = ((U64(i) + uuid_source_offset) & 1) == 0
                             ? &packed_source_a
                             : &packed_source_b;
    Benchmark::prevent_optimization(source);

    const Uuid uuid = Uuid::deserialize(*source);
    value ^= uuid.get_value().high;
  }

  Benchmark::prevent_optimization(value);
}

VALIDATION_BENCHMARK(SerializationUuid, validate_dashed_x1024) {
  Count value = 0;
  for (Count i = 0; i < uuid_batch; i++) {
    const auto* source = ((U64(i) + uuid_source_offset) & 1) == 0
                             ? &dashed_source_a
                             : &dashed_source_b;
    Benchmark::prevent_optimization(source);

    const Bool valid = Uuid::is_valid(*source);
    value += valid.value;
  }

  Benchmark::prevent_optimization(value);
}

VALIDATION_BENCHMARK(SerializationUuid, validate_packed_x1024) {
  Count value = 0;
  for (Count i = 0; i < uuid_batch; i++) {
    const auto* source = ((U64(i) + uuid_source_offset) & 1) == 0
                             ? &packed_source_a
                             : &packed_source_b;
    Benchmark::prevent_optimization(source);

    const Bool valid = Uuid::is_valid(*source);
    value += valid.value;
  }

  Benchmark::prevent_optimization(value);
}
