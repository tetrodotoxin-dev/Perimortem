// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include <string.h>

#include "perimortem/core/view/bytes.h"

namespace Perimortem::Core::Algorithm {

// Hash produces 64 bit values for byte sequences, numeric values, pointers, and
// types that provide a `hash` operation. Byte input uses two accumulation lanes
// and includes its length before the lanes are combined. These values serve
// runtime lookup tables and provide neither cryptographic protection nor a
// persistent identity contract.
class Hash {
  // `memcpy` produces the preferred runtime loads but cannot run during
  // constant evaluation. Clang does not replace the equivalent byte shifts with
  // those loads, so `if consteval` keeps the slower sequence out of runtime
  // code.
  template <Count chunks>
  static constexpr auto memcpy_64(U64 (&result)[chunks], const U8* data)
      -> void {
    if consteval {
      for (Count chunk = 0; chunk < chunks; chunk++) {
        result[chunk] = 0;
        result[chunk] |= U64(data[7]) << 56;
        result[chunk] |= U64(data[6]) << 48;
        result[chunk] |= U64(data[5]) << 40;
        result[chunk] |= U64(data[4]) << 32;
        result[chunk] |= U64(data[3]) << 24;
        result[chunk] |= U64(data[2]) << 16;
        result[chunk] |= U64(data[1]) << 8;
        result[chunk] |= U64(data[0]) << 0;

        data += sizeof(U64);
      }
    } else {
      memcpy(&result, data, sizeof(U64) * chunks);
    }
  }

  static constexpr auto hash_numeric(U64 numeric) -> U64 {
    U64 result = numeric * const_values[0];
    result ^= (result >> 33) * const_values[4];
    return result;
  }

 public:
  constexpr Hash(Core::View::Bytes bytes) {
    const U8* data = bytes.get_data();

    // Keys shorter than eight bytes use one packed value. Keeping the full
    // eight byte case in the remainder path lets that path handle backward tail
    // loads without another size check. This shape also produced the smallest
    // hot path and the best results over the benchmark corpus.
    U64 small_hash_value = 0;
    switch (bytes.get_size()) {
    case 7:
      small_hash_value |= U64(data[6]) << 48;
    case 6:
      small_hash_value |= U64(data[5]) << 40;
    case 5:
      small_hash_value |= U64(data[4]) << 32;
    case 4:
      small_hash_value |= U64(data[3]) << 24;
    case 3:
      small_hash_value |= U64(data[2]) << 16;
    case 2:
      small_hash_value |= U64(data[1]) << 8;
    case 1: {
      small_hash_value |= U64(data[0]);

      small_hash_value = const_values[0] ^ (small_hash_value + const_values[3]);
      small_hash_value *= const_values[2];

      // Mix both the packed bytes and key length so short keys with common
      // prefixes do not retain correlated low bits.
      value = small_hash_value ^ (bytes.get_size() * const_values[0]);
      value ^= value >> 27;
      value *= const_values[2];
      value ^= value >> 33;
      value *= const_values[4];
      value ^= value >> 33;
      return;
    }

    // Empty input uses one fixed value so callers receive a stable hash without
    // reading absent storage.
    case 0:
      value = const_values[4];
      return;
    }

    // Seed each accumulation lane independently before the bulk loop mixes
    // source words into it.
    U64 result[block_stride];
    const Count block_count = bytes.get_size() / sizeof(U64);
    for (Count i = 0; i < block_stride; i++) {
      result[i] = const_values[i];
    }

    // Two independent accumulation lanes cover sixteen bytes per iteration. The
    // loop fits in two decode blocks on Zen3 and newer processors
    // (https://godbolt.org/z/KzK6dPM4G). Integer multiplication remains the
    // largest pipeline stall, but improves distribution for longer keys in the
    // benchmark corpus.
    [[clang::code_align(32)]]
    for (Count processed = 0; processed + block_stride <= block_count;
         processed += block_stride) {
      // `memcpy_64` expresses potentially unaligned loads without selecting a
      // host instruction set. Explicit SSE loads were slightly slower while
      // processing two blocks in local benchmarks.
      U64 block[block_stride];
      memcpy_64(block, data);
      data += sizeof(U64) * block_stride;
      for (Count k = 0; k < block_stride; k++) {
        result[k] ^= block[k] + const_values[k + 3];
        result[k] *= const_values[k + 1];
      }
    }

    // The bulk loop leaves `data` at the next 16 byte boundary. Complete U64
    // copies handle every remaining size without reading beyond the source.
    // Tails longer than eight bytes load their final word before falling
    // through to load the leading word. Shorter tails read backward into the
    // processed block, then discard those repeated bytes.
    Count left_over = bytes.get_size() & (sizeof(U64) * block_stride - 1);
    switch (left_over) {
    case 15:
    case 14:
    case 13:
    case 12:
    case 11:
    case 10:
    case 9: {
      U64 remainder_tail[1];
      memcpy_64(remainder_tail, data + left_over - sizeof(U64));

      remainder_tail[0] >>= (sizeof(U64) * 2 - left_over) * 8;

      result[1] ^= remainder_tail[0] + const_values[1];
      result[1] *= const_values[2];

      [[fallthrough]];
    }

    case 8: {
      U64 remainder[1];
      memcpy_64(remainder, data);

      result[0] ^= remainder[0] + const_values[3];
      result[0] *= const_values[2];
      break;
    }

    case 7:
    case 6:
    case 5:
    case 4:
    case 3:
    case 2:
    case 1: {
      U64 remainder[1];
      memcpy_64(remainder, data + left_over - sizeof(U64));

      remainder[0] >>= (sizeof(U64) - left_over) * 8;

      result[0] ^= remainder[0] + const_values[3];
      result[0] *= const_values[2];
      break;
    }

    case 0:
      break;
    }

    // Include the source length while avalanching each lane, then fold both
    // lanes into one result. Length separates equal prefixes with different
    // extents.
    U64 final_result = result[0];
    for (Count i = 0; i < block_stride; i++) {
      result[i] ^= bytes.get_size() * const_values[0];
      result[i] ^= result[i] >> 27;
      result[i] *= const_values[2];
      result[i] ^= result[i] >> 33;
      result[i] *= const_values[4];
      result[i] ^= result[i] >> 33;
      final_result ^= result[i];
    }

    value = final_result;
  }

  constexpr Hash(S8 numeric) : value(hash_numeric(U64(numeric))) {}
  constexpr Hash(S16 numeric) : value(hash_numeric(U64(numeric))) {}
  constexpr Hash(S32 numeric) : value(hash_numeric(U64(numeric))) {}
  constexpr Hash(S64 numeric) : value(hash_numeric(U64(numeric))) {}
  constexpr Hash(U8 numeric) : value(hash_numeric(U64(numeric))) {}
  constexpr Hash(U16 numeric) : value(hash_numeric(U64(numeric))) {}
  constexpr Hash(U32 numeric) : value(hash_numeric(U64(numeric))) {}
  constexpr Hash(U64 numeric) : value(hash_numeric(U64(numeric))) {}
  template <typename pointer_type>
  Hash(pointer_type* pointer)
      : value(hash_numeric(U64(Data::address(pointer)))) {}

  // Nonpointer types can supply a type specific hash without exposing a byte
  // representation.
  template <typename hashable_type>
    requires(!__is_pointer(hashable_type))
  constexpr Hash(const hashable_type& obj) {
    value = obj.hash();
  }

  constexpr auto get_value() const -> U64 { return value; }

  // Avalanche another numeric hash before combining it with this value. The
  // ordered mixing prevents composite fields from collapsing to a plain XOR.
  constexpr auto Rehash(U64 hash) const -> U64 {
    U64 result;

    result = hash * const_values[0];
    result ^= result >> 27;
    result *= const_values[2];
    result ^= result >> 33;
    result *= const_values[4];
    result ^= result >> 33;
    result ^= value;
    return result;
  }

 private:
  // One or two lanes cover the useful benchmarked range. More lanes increased
  // generated code without improving throughput, so each chunk remains sixteen
  // bytes.
  static constexpr Count block_stride = 2;

  // Feedback benchmarking selected these constants for collision rate per CPU
  // cycle. They are runtime lookup parameters rather than a persistent hash
  // identity and may remain tuned to the benchmark corpus.
  static constexpr U64 const_values[5] = {
    0b00010101'01000101'10011011'00011001'10100101'11101101'01011101'10111011,
    0b10101010'10100011'01100000'11001011'00110001'10100110'00001101'01011101,
    0b00000000'00111011'11011100'01111101'11110101'11100110'00100001'01100101,
    0b01111111'11001100'01110011'01001100'10101010'01001010'10010001'11011101,
    0b00011111'11000010'00100001'01111001'00101110'01000000'11000001'10001001,
  };

  U64 value;
};

}  // namespace Perimortem::Core::Algorithm
