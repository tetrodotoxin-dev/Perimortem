// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/random.hpp"

#if defined(PERI_AVX2) || defined(PERI_LINUX) || defined(PERI_WINDOWS)
#include <immintrin.h>
#endif
#include <stdlib.h>
#ifdef PERI_WASM
#include <unistd.h>
#endif

#include "perimortem/core/data.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/null_terminated.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::System;

static constexpr Count channel_depth = 4;
static constexpr Count max_index = 4 * channel_depth;

struct PhiloxState {
  static constexpr Count round_count = 10;

#ifdef PERI_AVX2
  // Philox4x32 uses two multiplication constants and advances its key with two
  // Weyl constants after each round. Duplicating those pairs across the AVX2
  // lanes evaluates two independent Philox generators per vector.
  static constexpr __m256i philox4x32_constants = _mm256_set_epi64x(
      S64(0x00000000'D2511F53),
      S64(0x00000000'CD9E8D57),
      S64(0x00000000'D2511F53),
      S64(0x00000000'CD9E8D57));
  static constexpr __m256i philox4x32_xor_mask = _mm256_set_epi64x(
      S64(0xFFFFFFFF'00000000),
      S64(0xFFFFFFFF'00000000),
      S64(0xFFFFFFFF'00000000),
      S64(0xFFFFFFFF'00000000));
  static constexpr __m256i philox4x32_weyl = _mm256_set_epi64x(
      S64(0x9E2779B9'00000000),
      S64(0xBB67AE85'00000000),
      S64(0x9E2779B9'00000000),
      S64(0xBB67AE85'00000000));
  // Reorders the multiplied counter halves for the next round. The high half
  // crosses each 64 bit pair while the low half moves into the high position.
  static constexpr U8 counter_shuffle = 0b10'01'00'11;
#endif

  alignas(32) U64 output[max_index];
#ifdef PERI_AVX2
  __m256i dual_channel_key;
  __m256i dual_channel_counter;
#else
  U64 keys[2];
  U64 counters[4];
#endif
  Count index;
};

auto Random::read_entropy() -> U64 {
#ifdef PERI_WASM
  U64 value;
  if (getentropy(&value, sizeof(value)) != 0) {
    Diagnostics::Log::fatal("Unable to obtain runtime seed entropy."_view);
  }

  return value;
#else
  U64 value;
  Count timeout = 100000;
  while (!_rdrand64_step(&value) and timeout) {
    timeout -= 1;
  }

  // RDRAND can transiently fail. The bounded retry avoids hanging startup. The
  // C runtime fallback is only a last resort seed source and must not be
  // treated as cryptographic entropy.
  if (timeout == 0) {
    return (Count(rand()) << 32) | Count(rand());
  }

  return value;
#endif
}

// Advances four counter depths for each of the two vectorized Philox channels.
//
// One refill produces sixteen 64 bit values. All four depths must pass through
// every Philox round. Leaving depth zero as the raw counter would preserve
// uniqueness while destroying the statistical meaning of the generator.
static constexpr auto bump_counter(PhiloxState& state) -> void {
#ifdef PERI_AVX2
  __m256i philox_keys = state.dual_channel_key;
  __m256i philox_channels[channel_depth];
  philox_channels[0] = state.dual_channel_counter;
  for (Count i = 1; i < channel_depth; i++) {
    philox_channels[i] =
        _mm256_add_epi64(state.dual_channel_counter, _mm256_set1_epi64x(i));
  }

  for (Count round = 0; round < PhiloxState::round_count; round++) {
    for (Count i = 0; i < channel_depth; i++) {
      const auto hilo_mul = _mm256_mul_epu32(
          philox_channels[i], PhiloxState::philox4x32_constants);
      const auto xor_mask = _mm256_and_si256(
          philox_channels[i], PhiloxState::philox4x32_xor_mask);
      const auto eval = _mm256_xor_si256(hilo_mul, xor_mask);
      philox_channels[i] = _mm256_shuffle_epi32(
          _mm256_xor_si256(eval, philox_keys), PhiloxState::counter_shuffle);
    }

    if (round != PhiloxState::round_count - 1) {
      philox_keys = _mm256_add_epi32(philox_keys, PhiloxState::philox4x32_weyl);
    }
  }

  for (Count i = 0; i < channel_depth; i++) {
    _mm256_store_si256(
        Data::cast<__m256i>(state.output) + i, philox_channels[i]);
  }

  // The next refill starts after every counter consumed by this batch.
  state.dual_channel_counter = _mm256_add_epi64(
      state.dual_channel_counter, _mm256_set1_epi64x(channel_depth));
#else
  // Each AVX2 lane contains one four word Philox counter. Evaluate those same
  // lanes and depths in order so the scalar path preserves the generated
  // stream.
  for (Count depth = 0; depth < channel_depth; ++depth) {
    for (Count lane = 0; lane < 2; ++lane) {
      const U64 lower = state.counters[lane * 2] + depth;
      const U64 upper = state.counters[lane * 2 + 1] + depth;
      U32 a = U32(lower);
      U32 b = U32(lower >> 32);
      U32 c = U32(upper);
      U32 d = U32(upper >> 32);
      U32 key_lower = U32(state.keys[lane]);
      U32 key_upper = U32(state.keys[lane] >> 32);
      for (Count round = 0; round < PhiloxState::round_count; ++round) {
        const U64 first = U64(a) * 0xCD9E8D57;
        const U64 second = U64(c) * 0xD2511F53;
        a = U32(second >> 32) ^ d ^ key_upper;
        c = U32(first >> 32) ^ b ^ key_lower;
        b = U32(first);
        d = U32(second);
        key_lower += 0xBB67AE85;
        key_upper += 0x9E2779B9;
      }

      state.output[depth * 4 + lane * 2] = U64(a) | (U64(b) << 32);
      state.output[depth * 4 + lane * 2 + 1] = U64(c) | (U64(d) << 32);
    }
  }

  for (auto& counter : state.counters) {
    counter += channel_depth;
  }
#endif
  state.index = 0;
}

// Seeds independent keys and counters for one thread local generator. Keeping
// the state thread local avoids synchronization and false sharing in the hot
// generate() path.
static auto create_prng() -> PhiloxState {
  PhiloxState state;

  U64 keys[] = {Random::read_entropy(), Random::read_entropy()};
#ifdef PERI_AVX2
  state.dual_channel_key = _mm256_set_epi32(
      U32(keys[0] >> 32), 0, U32(keys[0]), 0, U32(keys[1] >> 32), 0,
      U32(keys[1]), 0);

  state.dual_channel_counter = _mm256_set_epi64x(
      Random::read_entropy(), Random::read_entropy(), Random::read_entropy(),
      Random::read_entropy());
#else
  state.keys[0] = keys[1];
  state.keys[1] = keys[0];
  for (Count index = 4; index > 0; --index) {
    state.counters[index - 1] = Random::read_entropy();
  }
#endif

  bump_counter(state);
  return state;
}

auto Random::generate() -> U64 {
  thread_local static PhiloxState engine = create_prng();
  if (engine.index == max_index) {
    bump_counter(engine);
  }

  return engine.output[engine.index++];
}
