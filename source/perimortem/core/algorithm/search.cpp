// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/algorithm/search.hpp"

#include "perimortem/core/storage/bytes.hpp"

using namespace Perimortem::Core;

#ifdef PERI_AVX2
#include <immintrin.h>
#endif

auto Algorithm::Search::match(View::Bytes src, View::Bytes value) -> Count {
  if (value.is_empty()) {
    return 0;
  }

  // A one byte query uses the dedicated byte scan without substring tail
  // filtering or candidate verification.
  if (value.get_size() == 1) {
    return match(src, value[0]);
  }

  if (value.get_size() > src.get_size()) {
    return Count(-1);
  }

  if (value.get_size() == src.get_size()) {
    return value == src ? 0 : Count(-1);
  }

  Count i = 0;
  const Count tail_offset = value.get_size() - 1;

#ifdef PERI_AVX2

  // Pad the query and its active byte mask to one AVX2 register. Requiring two
  // source registers keeps every candidate verification load inside the view,
  // including candidates near the end of the vector loop.
  constexpr auto vectorize_limit = sizeof(__m256i);
  if (src.get_size() >= vectorize_limit * 2 &&
      value.get_size() <= vectorize_limit) [[likely]] {
    Storage::Bytes<vectorize_limit> value_bytes = value;
    Storage::Bytes<vectorize_limit> value_filter;
    Data::set(value_filter.get_data(), 0xFF, value.get_size());

    const auto first_byte = _mm256_set1_epi8(value[0]);
    const auto last_byte = _mm256_set1_epi8(value[tail_offset]);
    const auto test_value =
        _mm256_loadu_si256(Data::cast<const __m256i_u>(value_bytes.get_data()));
    const auto test_filter = _mm256_loadu_si256(
        Data::cast<const __m256i_u>(value_filter.get_data()));

    // Compare the first and last query bytes at 32 candidate offsets. Only
    // offsets matching both endpoints load and compare the complete query.
    for (; i < src.get_size() - vectorize_limit * 2; i += vectorize_limit) {
      const auto head_block =
          _mm256_loadu_si256(Data::cast<const __m256i_u>(src.get_data() + i));
      const auto tail_block = _mm256_loadu_si256(
          Data::cast<const __m256i_u>(src.get_data() + i + tail_offset));

      const auto head_slots = _mm256_cmpeq_epi8(head_block, first_byte);
      const auto tail_slots = _mm256_cmpeq_epi8(tail_block, last_byte);
      const auto test_ranges = _mm256_and_si256(head_slots, tail_slots);
      auto range_mask = U32(_mm256_movemask_epi8(test_ranges));
      while (range_mask) {
        auto index = __builtin_ctzg(range_mask);
        range_mask ^= 1 << index;

        // Zero bytes beyond the target length before comparing the complete
        // register. One vector comparison can then validate the candidate
        // without constructing a separate short range.
        const auto test_block = _mm256_loadu_si256(
            Data::cast<const __m256i_u>(src.get_data() + i + index));
        const auto possible_match = _mm256_and_si256(test_block, test_filter);
        const auto match_value = _mm256_cmpeq_epi8(possible_match, test_value);
        auto match = U32(_mm256_movemask_epi8(match_value));
        if (match == 0xFFFFFFFF) {
          return i + index;
        }
      }
    }
  }

#endif

  // The same scan handles the vector tail and targets without AVX2.
  for (; i < src.get_size() - tail_offset; i++) {
    // Skip unless both head and tail bytes match. Any mismatch rules out this
    // position without touching the middle bytes.
    if (src[i] != value[0] || src[i + tail_offset] != value[tail_offset]) {
      continue;
    }

    Bool found = True;
    for (Count j = 1; j < tail_offset; j++) {
      if (src[i + j] != value[j]) {
        found = False;
        break;
      }
    }

    if (found) {
      return i;
    }
  }

  return Count(-1);
}

auto Algorithm::Search::match(View::Bytes src, U8 value) -> Count {
  if (src.is_empty()) {
    return Count(-1);
  }

  // Byte queries need one equality mask per block and no separate candidate
  // verification.
  Count i = 0;
  auto source_data = src.get_data();

#ifdef PERI_AVX2

  constexpr auto vectorize_limit = sizeof(__m256i);
  if (src.get_size() >= vectorize_limit) [[likely]] {
    const auto test_mask = _mm256_set1_epi8(value);

    // Each comparison covers 32 consecutive bytes. The least set mask bit gives
    // the first matching offset and preserves first occurrence behavior.
    for (; i < src.get_size() - vectorize_limit; i += vectorize_limit) {
      const auto source_block =
          _mm256_loadu_si256(Data::cast<const __m256i_u>(source_data + i));

      const auto source_hits = _mm256_cmpeq_epi8(source_block, test_mask);
      const auto range_mask = U32(_mm256_movemask_epi8(source_hits));
      if (range_mask) {
        auto index = __builtin_ctzg(range_mask);
        return i + index;
      }
    }
  }

#endif

  // Scan bytes not covered by the vector loop. This is also the complete path
  // on targets without AVX2.
  for (; i < src.get_size(); i++) {
    if (value == source_data[i]) {
      return i;
    }
  }

  return Count(-1);
}
