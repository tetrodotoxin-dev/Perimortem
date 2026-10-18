// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/random.hpp"

#if defined(PERI_AVX2) || defined(TOOLCHAIN_LINUX) || defined(TOOLCHAIN_WINDOWS)
#include <immintrin.h>
#endif
#ifdef TOOLCHAIN_WINDOWS
#include <intrin.h>
#include <windows.h>
#endif
#ifdef TOOLCHAIN_LINUX
#include <cpuid.h>
#include <errno.h>
#include <sys/random.h>
#include <time.h>
#endif
#ifdef TOOLCHAIN_WASM
#include <time.h>
#include <unistd.h>
#endif

#include "perimortem/core/data.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/diagnostics/time.hpp"
#include "perimortem/core/view/null_terminated.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Serialization;
using namespace Perimortem::System;

static constexpr Count channel_depth = 4;
static constexpr Count max_index = 4 * channel_depth;
static constexpr Count seed_word_count = 6;
static U64 seed_sequence = 0;

#if defined(TOOLCHAIN_LINUX) || defined(TOOLCHAIN_WINDOWS)

static U8 host_fallback_reported = 0;

#endif

static U8 runtime_fallback_reported = 0;

struct PhiloxSeed {
  U64 values[seed_word_count];
};

enum class HardwareSeedResult : U8 {
  Complete,
  Unsupported,
  Failed,
};

// UUID version 7 encodes Unix milliseconds as part of its wire format. Keeping
// this wall clock read private prevents its stamp from being compared with the
// public monotonic clock used for interval measurement.
static auto unix_milliseconds() -> U64 {

#ifdef TOOLCHAIN_WINDOWS

  FILETIME stamp;
  GetSystemTimePreciseAsFileTime(&stamp);

  const U64 ticks = (U64(stamp.dwHighDateTime) << 32) | stamp.dwLowDateTime;
  return (ticks - 116444736000000000ULL) / 10'000;

#else

  timespec stamp;
  clock_gettime(CLOCK_REALTIME, &stamp);
  return U64(stamp.tv_sec) * 1'000 + U64(stamp.tv_nsec) / 1'000'000;

#endif
}

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

// Native x86 hosts use the hardware generator as the shortest seed path. The
// feature check avoids executing RDRAND on older processors, while bounded
// retries handle the instruction's documented transient failure.
static auto read_hardware_seed(PhiloxSeed& seed) -> HardwareSeedResult {

#if defined(TOOLCHAIN_LINUX) || defined(TOOLCHAIN_WINDOWS)

#ifdef TOOLCHAIN_WINDOWS

  S32 registers[4];
  __cpuid(registers, 1);
  if ((U32(registers[2]) & (U32(1) << 30)) == 0) {
    return HardwareSeedResult::Unsupported;
  }

#else

  U32 eax, ebx, ecx, edx;
  if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx) || !(ecx & bit_RDRND)) {
    return HardwareSeedResult::Unsupported;
  }

#endif

  for (Count word = 0; word < seed_word_count; word++) {
    Bool read = False;
    for (Count attempt = 0; attempt < 10; attempt++) {
      if (_rdrand64_step(&seed.values[word])) {
        read = True;
        break;
      }
    }

    if (!read) {
      return HardwareSeedResult::Failed;
    }
  }

  return HardwareSeedResult::Complete;

#else

  return HardwareSeedResult::Unsupported;

#endif
}

// Platforms with a direct entropy syscall can still seed Philox when hardware
// generation is unavailable. Keeping this operation private avoids presenting
// Random as a general cryptographic entropy API.
static auto read_host_seed(PhiloxSeed& seed) -> Bool {

#ifdef TOOLCHAIN_LINUX

  U8* output = Data::cast<U8>(&seed);
  Count written = 0;
  while (written < sizeof(seed)) {
    const auto result =
        getrandom(output + written, CppSize(sizeof(seed) - written), 0);
    if (result > 0) {
      written += Count(result);
      continue;
    }

    if (result < 0 && errno == EINTR) {
      continue;
    }

    return False;
  }

  return True;

#elifdef TOOLCHAIN_WASM

  return getentropy(&seed, sizeof(seed)) == 0;

#else

  return False;

#endif
}

// The SplitMix64 avalanche diffuses every changing input bit across one seed
// word without adding state to the Philox generator.
static constexpr auto mix_seed(U64 value) -> U64 {
  value ^= value >> 30;
  value *= 0xBF58476D1CE4E5B9ULL;
  value ^= value >> 27;
  value *= 0x94D049BB133111EBULL;
  return value ^ (value >> 31);
}

// Hardware and host entropy are always mixed with runtime noise. The process
// sequence separates initialization attempts when clock samples repeat, while
// monotonic time, Unix time and the stack address add variation across threads
// and process starts.
static auto mix_runtime_noise(PhiloxSeed& seed) -> void {
  U64 state = Diagnostics::Time::now().get_nanoseconds();
  state ^= unix_milliseconds() * 0x9E3779B97F4A7C15ULL;
  state ^= U64(Data::address(&seed));
  state ^=
      mix_seed(__atomic_fetch_add(&seed_sequence, U64(1), __ATOMIC_RELAXED));

  for (Count index = 0; index < seed_word_count; index++) {
    state += 0x9E3779B97F4A7C15ULL;
    seed.values[index] ^= mix_seed(state);
  }
}

// A fallback changes which external source contributes seed bytes but does not
// make the noncryptographic generator unusable. Report host entropy and runtime
// noise once per process so diagnostics preserve that change without one
// warning per thread.
static auto report_seed_fallback(U8& reported, View::Bytes message) -> void {
  if (__atomic_exchange_n(&reported, U8(1), __ATOMIC_RELAXED) == 0) {
    Diagnostics::Log::warning(message);
  }
}

static auto create_seed() -> PhiloxSeed {
  PhiloxSeed seed = {};
  const HardwareSeedResult hardware = read_hardware_seed(seed);
  if (hardware != HardwareSeedResult::Complete) {
    if (read_host_seed(seed)) {

#if defined(TOOLCHAIN_LINUX) || defined(TOOLCHAIN_WINDOWS)

      const View::Bytes message =
          hardware == HardwareSeedResult::Failed
              ? "System::Random seed uses host entropy because RDRAND failed "
                "after bounded retries."_view
              : "System::Random seed uses host entropy because RDRAND is "
                "unavailable."_view;
      report_seed_fallback(host_fallback_reported, message);

#endif

    } else {
      const View::Bytes message =
          hardware == HardwareSeedResult::Failed
              ? "System::Random seed uses runtime process noise because "
                "RDRAND failed after bounded retries and host entropy is "
                "unavailable."_view
              : "System::Random seed uses runtime process noise because "
                "hardware and host entropy are unavailable."_view;
      report_seed_fallback(runtime_fallback_reported, message);
    }
  }

  mix_runtime_noise(seed);
  return seed;
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
  const PhiloxSeed seed = create_seed();

#ifdef PERI_AVX2

  state.dual_channel_key = _mm256_set_epi32(
      U32(seed.values[0] >> 32), 0, U32(seed.values[0]), 0,
      U32(seed.values[1] >> 32), 0, U32(seed.values[1]), 0);

  state.dual_channel_counter = _mm256_set_epi64x(
      seed.values[2], seed.values[3], seed.values[4], seed.values[5]);

#else

  state.keys[0] = seed.values[1];
  state.keys[1] = seed.values[0];
  for (Count index = 0; index < 4; index++) {
    state.counters[index] = seed.values[index + 2];
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

auto Random::generate_uuid_v4() -> Uuid {

#ifdef PERI_AVX2

  const auto value = _mm_set_epi64x(generate(), generate());
  const auto v4_mask =
      _mm_set_epi64x(0xFFFFFFFFFFFF0FFFull, 0x3FFFFFFFFFFFFFFFull);
  const auto v4_set =
      _mm_set_epi64x(0x0000000000004000ull, 0x8000000000000000ull);
  const auto identified = _mm_or_si128(_mm_and_si128(value, v4_mask), v4_set);

  U64 values[2];
  _mm_storeu_si128(Data::cast<__m128i>(values), identified);
  return Uuid(values[1], values[0]);

#else

  const U64 high = generate();
  const U64 low = generate();
  return Uuid(
      (high & 0xFFFFFFFFFFFF0FFFULL) | 0x4000,
      (low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL);

#endif
}

auto Random::generate_uuid_v7() -> Uuid {
  const U64 timestamp = unix_milliseconds();

#ifdef PERI_AVX2

  const U64 time_and_random =
      ((timestamp & 0xFFFFFFFFFFFF) << 16) | (generate() & 0xFFFF);
  const auto value = _mm_set_epi64x(time_and_random, generate());
  const auto v7_mask =
      _mm_set_epi64x(0xFFFFFFFFFFFF0FFFull, 0x3FFFFFFFFFFFFFFFull);
  const auto v7_set =
      _mm_set_epi64x(0x0000000000007000ull, 0x8000000000000000ull);
  const auto identified = _mm_or_si128(_mm_and_si128(value, v7_mask), v7_set);

  U64 values[2];
  _mm_storeu_si128(Data::cast<__m128i>(values), identified);
  return Uuid(values[1], values[0]);

#else

  const U64 high =
      ((timestamp & 0xFFFFFFFFFFFF) << 16) | (generate() & 0xFFF) | 0x7000;
  const U64 low = (generate() & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
  return Uuid(high, low);

#endif
}
