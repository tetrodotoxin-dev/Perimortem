// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/serialization/formats/uuid.hpp"

#include "perimortem/core/data.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Serialization;

#ifdef PERI_AVX2

#include <immintrin.h>

static constexpr S8 null = 0x80;

// Separates each packed byte into its high and low hexadecimal nibbles. The
// shuffle orders the two 128 bit lanes so serialization observes the high UUID
// word before the low word.
static constexpr auto nibbler(__m128i packed_uuid) -> __m256i {
  const auto nibble_mask = _mm256_set1_epi8(0x0F);
  const auto nibble_high = _mm_srli_epi64(packed_uuid, 4);
  const auto two_byte_pack = _mm256_and_si256(
      _mm256_set_m128i(
          _mm_unpackhi_epi8(packed_uuid, nibble_high),
          _mm_unpacklo_epi8(packed_uuid, nibble_high)),
      nibble_mask);

  // Mirroring each lane places the high nibble before the low nibble.
  const auto mirror = _mm256_set_epi8(
      // Lower
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
      // Upper
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
  return _mm256_shuffle_epi8(two_byte_pack, mirror);
}

// Maps nibbles zero through fifteen to lowercase hexadecimal ASCII. Decimal
// lanes receive the `0` base while the remaining lanes receive the additional
// offset to `a`.
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

// Hexadecimal digits store their value in the low nibble. Alphabetic ASCII
// stores one through six there, so adding nine to bytes above `9` maps both
// letter cases to ten through fifteen with one vector operation.
static constexpr auto convert_to_nibble(__m256i ascii) -> __m256i {
  const auto nibble_mask = _mm256_set1_epi8(0x0F);
  const auto nibbles = _mm256_and_si256(ascii, nibble_mask);
  const auto numeric_cutoff = _mm256_set1_epi8('9');
  const auto alpha_slots = _mm256_cmpgt_epi8(ascii, numeric_cutoff);
  const auto alpha_offset = _mm256_set1_epi8(9);
  const auto slot_offsets = _mm256_and_si256(alpha_slots, alpha_offset);
  return _mm256_add_epi8(nibbles, slot_offsets);
}

// Selects the 32 hexadecimal bytes from groups of eight, four, four, four and
// twelve digits.
static auto load_dashed_ascii(const U8* source) -> __m256i {
  const auto buffer = _mm256_loadu_si256(Data::cast<const __m256i_u>(source));
  const auto offset_buffer =
      _mm256_loadu_si256(Data::cast<const __m256i_u>(source + 4));

  const auto packing_shuffle = _mm256_set_epi8(
      null, null, null, null, 15, 14, 13, 12, 11, 10, 9, 8, 6, 5, 4, 3, null,
      null, 15, 14, 12, 11, 10, 9, 7, 6, 5, 4, 3, 2, 1, 0);
  const auto packed_bytes = _mm256_shuffle_epi8(buffer, packing_shuffle);

  const auto dash_shuffle = _mm256_set_epi8(
      // Lane 1
      15, 14, 13, 12, null, null, null, null, null, null, null, null, null,
      null, null, null,
      // Lane 2
      13, 12, null, null, null, null, null, null, null, null, null, null, null,
      null, null, null);
  const auto dash_fill = _mm256_shuffle_epi8(offset_buffer, dash_shuffle);
  return _mm256_or_si256(packed_bytes, dash_fill);
}

// Combines the decimal, uppercase and lowercase ranges so movemask returns all
// ones exactly when every vector lane contains one hexadecimal ASCII byte.
static auto valid_hexadecimal(__m256i ascii) -> Bool {
  const auto digits = _mm256_and_si256(
      _mm256_cmpgt_epi8(ascii, _mm256_set1_epi8('0' - 1)),
      _mm256_cmpgt_epi8(_mm256_set1_epi8('9' + 1), ascii));
  const auto uppercase = _mm256_and_si256(
      _mm256_cmpgt_epi8(ascii, _mm256_set1_epi8('A' - 1)),
      _mm256_cmpgt_epi8(_mm256_set1_epi8('F' + 1), ascii));
  const auto lowercase = _mm256_and_si256(
      _mm256_cmpgt_epi8(ascii, _mm256_set1_epi8('a' - 1)),
      _mm256_cmpgt_epi8(_mm256_set1_epi8('f' + 1), ascii));
  const auto valid =
      _mm256_or_si256(digits, _mm256_or_si256(uppercase, lowercase));
  return U32(_mm256_movemask_epi8(valid)) == U32(-1);
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

auto Formats::Uuid::parse(const Storage::Bytes<36>& uuid_string)
    -> Formats::Uuid {
  Formats::Uuid result;

#ifdef PERI_AVX2

  const auto* source = uuid_string.get_data();
  if (source[8] != '-' || source[13] != '-' || source[18] != '-' ||
      source[23] != '-') {
    return Formats::Uuid();
  }

  const auto ascii = load_dashed_ascii(source);
  if (!valid_hexadecimal(ascii)) {
    return Formats::Uuid();
  }

  deserialize_ascii(ascii, result.value);

#else

  const auto is_hex = [](U8 byte) -> Bool {
    return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') ||
           (byte >= 'A' && byte <= 'F');
  };

  Count digits = 0;
  for (Count index = 0; index < uuid_string.get_size(); ++index) {
    if (index == 8 || index == 13 || index == 18 || index == 23) {
      if (uuid_string[index] != '-') {
        return Formats::Uuid();
      }

      continue;
    }

    if (!is_hex(uuid_string[index])) {
      return Formats::Uuid();
    }

    U64& word = digits++ < 16 ? result.value.high : result.value.low;
    word = (word << 4) | ascii_to_nibble(uuid_string[index]);
  }

#endif

  return result;
}

auto Formats::Uuid::validate(View::Bytes source) -> Bool {

#ifdef PERI_AVX2

  if (source.get_size() == 32) {
    const auto ascii =
        _mm256_loadu_si256(Data::cast<const __m256i_u>(source.get_data()));
    return valid_hexadecimal(ascii);
  }

  if (source.get_size() == 36) {
    const auto* data = source.get_data();
    if (data[8] != '-' || data[13] != '-' || data[18] != '-' ||
        data[23] != '-') {
      return False;
    }

    return valid_hexadecimal(load_dashed_ascii(data));
  }

  return False;

#else

  const auto is_hex = [](U8 byte) -> Bool {
    return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') ||
           (byte >= 'A' && byte <= 'F');
  };

  if (source.get_size() == 32) {
    for (Count index = 0; index < source.get_size(); index++) {
      if (!is_hex(source[index])) {
        return False;
      }
    }

    return True;
  }

  if (source.get_size() == 36) {
    for (Count index = 0; index < source.get_size(); index++) {
      const Bool separator =
          index == 8 || index == 13 || index == 18 || index == 23;
      if (separator ? source[index] != '-' : !is_hex(source[index])) {
        return False;
      }
    }

    return True;
  }

  return False;

#endif
}

auto Formats::Uuid::parse(const Storage::Bytes<32>& uuid_string)
    -> Formats::Uuid {
  Formats::Uuid result;

#ifdef PERI_AVX2

  const auto ascii_buffer =
      _mm256_loadu_si256(Data::cast<const __m256i_u>(uuid_string.get_data()));
  if (!valid_hexadecimal(ascii_buffer)) {
    return Formats::Uuid();
  }

  deserialize_ascii(ascii_buffer, result.value);

#else

  const auto is_hex = [](U8 byte) -> Bool {
    return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') ||
           (byte >= 'A' && byte <= 'F');
  };

  for (Count index = 0; index < uuid_string.get_size(); ++index) {
    if (!is_hex(uuid_string[index])) {
      return Formats::Uuid();
    }

    U64& word = index < 16 ? result.value.high : result.value.low;
    word = (word << 4) | ascii_to_nibble(uuid_string[index]);
  }

#endif

  return result;
}

auto Formats::Uuid::serialize() const -> const Storage::Bytes<36> {
  Storage::Bytes<36> uuid_string;
  auto byte_buffer = uuid_string.get_access().get_data();

#ifdef PERI_AVX2

  const __m128i packed = _mm_loadu_si128(Data::cast<const __m128i>(&value));

  const auto nibbles = nibbler(packed);
  const auto ascii = convert_to_ascii(nibbles);

  // The lane local shuffle inserts four dash slots and leaves six characters
  // for scalar stores: two at the lane boundary and the final four characters.
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

  // The vector store writes the first 32 output bytes, including every dash.
  _mm256_storeu_si256(Data::cast<__m256i_u>(byte_buffer), dashed_ascii);

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
