// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/uuid.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/time.hpp"

#include "perimortem/system/random.hpp"

using namespace Perimortem::System;
using namespace Perimortem::Core;

#ifdef PERI_AVX2
#include <immintrin.h>

static constexpr S8 null = 0x80;

static auto generate_uuid_v4() -> __m128i {
  const auto value = _mm_set_epi64x(Random::generate(), Random::generate());

  const auto v4_mask =
      _mm_set_epi64x(0xFFFFFFFFFFFF0FFFull, 0x3FFFFFFFFFFFFFFFull);
  const auto v4_set =
      _mm_set_epi64x(0x0000000000004000ull, 0x8000000000000000ull);
  return _mm_or_si128(_mm_and_si128(value, v4_mask), v4_set);
}

static auto generate_uuid_v7() -> __m128i {
  // A retained identifier needs a timestamp with the same origin after a
  // restart. The wall clock supplies Unix time, while Time::now measures
  // intervals within the current boot.
  const U64 timestamp = Time::clock().get_stamp() / 1'000'000;
  const U64 time_and_random =
      ((timestamp & 0xFFFFFFFFFFFF) << 16) | (Random::generate() & 0xFFFF);
  const auto value = _mm_set_epi64x(time_and_random, Random::generate());

  const auto v7_mask =
      _mm_set_epi64x(0xFFFFFFFFFFFF0FFFull, 0x3FFFFFFFFFFFFFFFull);
  const auto v7_set =
      _mm_set_epi64x(0x0000000000007000ull, 0x8000000000000000ull);
  return _mm_or_si128(_mm_and_si128(value, v7_mask), v7_set);
}

// Expand 128 bits into nibbles spread across 256 bits.
// Bytes are packed from:
// [hhhhllll][hhhhllll][hhhhllll][hhhhllll]...
// [____hhhh][____llll][____hhhh][____llll]...
static constexpr auto nibbler(__m128i packed_guid) -> __m256i {
  const auto nibble_mask = _mm256_set1_epi8(0x0F);
  const auto nibble_high = _mm_srli_epi64(packed_guid, 4);
  const auto two_byte_pack = _mm256_and_si256(
      _mm256_set_m128i(
          _mm_unpackhi_epi8(packed_guid, nibble_high),
          _mm_unpacklo_epi8(packed_guid, nibble_high)),
      nibble_mask);

  // Convert to big endian with a shuffle by mirroring the nibbles.
  const auto mirror = _mm256_set_epi8(
      // Lower
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
      // Upper
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
  return _mm256_shuffle_epi8(two_byte_pack, mirror);
}

// Takes a set of nibbles and applies an offset to them to shift them into
// their ascii equivilant value (alphas are shifted to their lower case value).
static constexpr auto convert_to_ascii(__m256i nibbles) -> __m256i {
  const auto numeric_cutoff = _mm256_set1_epi8(0x09);
  const auto ascii_shift = _mm256_set1_epi8('0');
  const auto alpha_values = _mm256_add_epi8(nibbles, ascii_shift);

  const auto ascii_offset = _mm256_set1_epi8('a' - '9' - 1);
  const auto alpha_slots = _mm256_cmpgt_epi8(nibbles, numeric_cutoff);
  const auto slot_offsets = _mm256_and_si256(alpha_slots, ascii_offset);

  const auto final_values = _mm256_add_epi8(alpha_values, slot_offsets);
  return final_values;
}

// Takes a set of nibbles and applies an offset to them to shift them into
// their ascii equivilant value (alphas are shifted to their lower case value).
static constexpr auto convert_to_nibble(__m256i ascii) -> __m256i {
  const auto numeric_cutoff = _mm256_set1_epi8('9');
  const auto ascii_shift = _mm256_set1_epi8('0');
  const auto alpha_values = _mm256_sub_epi8(ascii, ascii_shift);

  const auto ascii_offset = _mm256_set1_epi8('a' - '9' - 1);
  const auto alpha_slots = _mm256_cmpgt_epi8(ascii, numeric_cutoff);
  const auto slot_offsets = _mm256_and_si256(alpha_slots, ascii_offset);

  const auto final_values = _mm256_sub_epi8(alpha_values, slot_offsets);
  return final_values;
}

static constexpr auto deserialize_ascii(
    __m256i ascii_buffer,
    perimortem_uuid& value) -> void {
  const auto nibbles = convert_to_nibble(ascii_buffer);
  auto nibble_high = _mm256_slli_epi16(nibbles, 12);
  auto spaced_bytes = _mm256_or_si256(nibbles, nibble_high);
  const auto packing_shuffle = _mm256_set_epi8(
      // Lower
      null, null, null, null, null, null, null, null, 1, 3, 5, 7, 9, 11, 13, 15,
      // Upper
      null, null, null, null, null, null, null, null, 1, 3, 5, 7, 9, 11, 13,
      15);
  auto packed_bytes = _mm256_shuffle_epi8(spaced_bytes, packing_shuffle);

  value.low = _mm256_extract_epi64(packed_bytes, 2);
  value.high = _mm256_extract_epi64(packed_bytes, 0);
}

#endif

auto Uuid::deserialize(const Static::Bytes<36>& uuid_string) -> Uuid& {
#ifdef PERI_AVX2
  // RFC 4122 groups hexadecimal digits in widths of eight, four, four, four,
  // and twelve.
  const auto buffer =
      _mm256_loadu_si256(Data::cast<const __m256i_u>(uuid_string.get_data()));
  const auto offset_buffer = _mm256_loadu_si256(
      Data::cast<const __m256i_u>(uuid_string.get_data() + 4));

  const auto packing_shuffle = _mm256_set_epi8(
      null, null, null, null, 15, 14, 13, 12, 11, 10, 9, 8, 6, 5, 4, 3, null,
      null, 15, 14, 12, 11, 10, 9, 7, 6, 5, 4, 3, 2, 1, 0);

  auto packed_bytes = _mm256_shuffle_epi8(buffer, packing_shuffle);
  const auto dash_shuffle = _mm256_set_epi8(
      // Lane 1
      15, 14, 13, 12, null, null, null, null, null, null, null, null, null,
      null, null, null,
      // Lane 2
      13, 12, null, null, null, null, null, null, null, null, null, null, null,
      null, null, null);
  auto dash_fill = _mm256_shuffle_epi8(offset_buffer, dash_shuffle);
  auto ascii_buffer = _mm256_or_si256(packed_bytes, dash_fill);

  deserialize_ascii(ascii_buffer, this->value);
#else
  // Keep the four separators outside the hexadecimal digit count so the two
  // words use the same ordering as constant evaluation and the C carrier.
  value.high = 0;
  value.low = 0;
  Count digits = 0;
  for (Count index = 0; index < uuid_string.get_size(); ++index) {
    if (index == 8 || index == 13 || index == 18 || index == 23) {
      continue;
    }

    U64& word = digits++ < 16 ? value.high : value.low;
    word = (word << 4) | ascii_to_nibble(uuid_string[index]);
  }
#endif
  return *this;
}

auto Uuid::deserialize(const Static::Bytes<32>& uuid_string) -> Uuid& {
#ifdef PERI_AVX2
  const auto ascii_buffer =
      _mm256_loadu_si256(Data::cast<const __m256i_u>(uuid_string.get_data()));

  deserialize_ascii(ascii_buffer, this->value);
#else
  value.high = 0;
  value.low = 0;
  for (Count index = 0; index < uuid_string.get_size(); ++index) {
    U64& word = index < 16 ? value.high : value.low;
    word = (word << 4) | ascii_to_nibble(uuid_string[index]);
  }
#endif
  return *this;
}

auto Uuid::serialize() const -> const Static::Bytes<36> {
  Static::Bytes<36> uuid_string;
  auto byte_buffer = uuid_string.get_access().get_data();

#ifdef PERI_AVX2
  const __m128i packed = _mm_loadu_si128(Data::cast<const __m128i>(&value));

  const auto nibbles = nibbler(packed);
  const auto ascii = convert_to_ascii(nibbles);

  // Space out the ascii and add in the dashes.
  // This drop a total of 6 bytes that need to be filled in later (4 for the
  // dashes and 2 since we can't shuffle across lanes),
  const auto dash_spacing = _mm256_set_epi8(
      // Lane 1
      11, 10, 9, 8, 7, 6, 5, 4, null, 3, 2, 1, 0, null, null, null,
      // Lane 2
      13, 12, null, 11, 10, 9, 8, null, 7, 6, 5, 4, 3, 2, 1, 0);
  const auto dashes = _mm256_set_epi8(
      // Lane 1
      0, 0, 0, 0, 0, 0, 0, 0, '-', 0, 0, 0, 0, '-', 0, 0,
      // Lane 2
      0, 0, '-', 0, 0, 0, 0, '-', 0, 0, 0, 0, 0, 0, 0, 0);
  auto spaced_ascii = _mm256_shuffle_epi8(ascii, dash_spacing);
  auto dashed_ascii = _mm256_or_si256(spaced_ascii, dashes);

  // Stamp as much data as we can.
  _mm256_storeu_si256(Data::cast<__m256i_u>(byte_buffer), dashed_ascii);

  // Stamp the dropped ascii into the output.
  // Since shuffle only works on 128 bit lanes for AVX we need to do a stamp for
  // each lane to capture the data that was pushed out.
  U16 dropped_2 = _mm256_extract_epi16(ascii, 7);
  U32 last_4 = _mm256_extract_epi32(ascii, 7);
  Data::copy(byte_buffer + 16, dropped_2);
  Data::copy(byte_buffer + 32, last_4);
#else
  constexpr char hexadecimal[] = "0123456789abcdef";
  Count digits = 0;
  for (Count index = 0; index < uuid_string.get_size(); ++index) {
    if (index == 8 || index == 13 || index == 18 || index == 23) {
      byte_buffer[index] = '-';
      continue;
    }

    const U64 word = digits < 16 ? value.high : value.low;
    byte_buffer[index] = hexadecimal[(word >> (60 - (digits % 16) * 4)) & 15];
    ++digits;
  }
#endif
  return uuid_string;
}

auto Uuid::generate_v4() -> Uuid {
#ifdef PERI_AVX2
  U64 values[2];
  _mm_storeu_si128(Data::cast<__m128i>(values), generate_uuid_v4());
  return Uuid(values[1], values[0]);
#else
  const U64 high = Random::generate();
  const U64 low = Random::generate();
  return Uuid(
      (high & 0xFFFFFFFFFFFF0FFFULL) | 0x4000,
      (low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL);
#endif
}

auto Uuid::generate_v7() -> Uuid {
#ifdef PERI_AVX2
  U64 values[2];
  _mm_storeu_si128(Data::cast<__m128i>(values), generate_uuid_v7());
  return Uuid(values[1], values[0]);
#else
  const U64 timestamp = Time::clock().get_stamp() / 1'000'000;
  const U64 high = ((timestamp & 0xFFFFFFFFFFFF) << 16) |
                   (Random::generate() & 0xFFF) | 0x7000;
  const U64 low =
      (Random::generate() & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
  return Uuid(high, low);
#endif
}
