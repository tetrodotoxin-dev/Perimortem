// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/serialization/encodings/base64.hpp"

#ifdef PERI_AVX2
#include <immintrin.h>
#endif

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::Serialization;

// The AVX lookup tables and shuffles don't use every slot in the array. Zero is
// useful but causes noise as zero is an actual valid value in some cases.
// Instead define the commonly used `_` to signal that a slot shouldn't be used.
#define _ 0

// C++ does not support C99 array designators such as `[character] = value`.
// Every unused byte remains explicit so each source character keeps its
// intended lookup index.
constexpr U8 decode_lookup[256] = {
  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
  _,  _,  _,  _,  _,  _,  _,  62, _,  _,  _,  63, 52, 53, 54, 55, 56, 57,
  58, 59, 60, 61, _,  _,  _,  _,  _,  _,  _,  0,  1,  2,  3,  4,  5,  6,
  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
  25, _,  _,  _,  _,  _,  _,  26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
  37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51};

constexpr U8 encode_lookup[64] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
  'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
  'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'};

static constexpr auto is_base64_character(U8 byte) -> Bool {
  return Bool(
      (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
      (byte >= '0' && byte <= '9') || byte == '+' || byte == '/');
}

static auto validate_decode(View::Bytes source) -> Bool {
  if (source.is_empty()) {
    return True;
  }

  if (source.get_size() % 4 != 0) {
    return False;
  }

  Count padding = 0;
  if (source[source.get_size() - 1] == '=') {
    padding++;
  }

  if (source[source.get_size() - 2] == '=') {
    padding++;
  }

  const Count content_size = source.get_size() - padding;
  for (Count index = 0; index < content_size; index++) {
    if (source[index] == '=') {
      return False;
    }

    if (!is_base64_character(source[index])) {
      return False;
    }
  }

  if (padding == 2 && (decode_lookup[source[content_size - 1]] & 0x0F) != 0) {
    return False;
  }

  if (padding == 1 && (decode_lookup[source[content_size - 1]] & 0x03) != 0) {
    return False;
  }

  return True;
}

// Extra bytes needed after the true output size for vectorized working space.

#ifdef PERI_AVX2

constexpr Count decode_extra_bytes = sizeof(__m256i) / 4;

#else

// The scalar tail writes all three candidate bytes before the published size
// removes bytes hidden by trailing padding, so it needs two scratch bytes.
constexpr Count decode_extra_bytes = 2;

#endif

auto vectorized_decode(U8* text, View::Bytes source) -> Count {

#ifdef PERI_AVX2

  // AMD processors without AVX512 can still use their additional AVX2 ports.
  // Unrolling three independent buffers also lets other modern processors
  // overlap the operations while retaining the broader AVX2 host support.
  //
  // Local testing found the gather and bitmask phases slower than native
  // AVX512. Avoiding the AVX512 power penalty still made this path faster on
  // current Intel processors.
  constexpr auto fused_channels = 3;
  constexpr auto avx2_channel_width = sizeof(__m256i);
  constexpr auto full_channel_width = avx2_channel_width * fused_channels;

#endif

  constexpr Count output_stride = 3;
  constexpr Count source_stride = 4;

  // Each gather writes one quarter of an AVX2 channel as zero scratch space.
  // The following channel overwrites that space, leaving only eight scratch
  // bytes after the final store instead of the sixteen required by AVX512.
  // Measurements on a 9950X3D indicate that shared data dependencies still
  // prevent full throughput from all three channels.
  Count size = (source.get_size() / source_stride) * output_stride;

  auto source_data = source.get_data();
  auto source_bytes = source.get_size();

  // Each trailing padding byte removes one byte from the published result and
  // keeps that final quartet out of a complete vector batch.
  for (Count i = 0; i < 2; i++) {
    if (source[source.get_size() - 1 - i] != '=') {
      break;
    }

    size--;
    source_bytes--;
  }

#ifdef PERI_AVX2

  // Keep a padded final quartet in the scalar tail. Including it in a full
  // vector batch would consume more bytes than remain after removing '='.
  const auto vectorized_chunks = source_bytes / full_channel_width;
  const auto source_vectorized_bytes = vectorized_chunks * full_channel_width;
  auto output_stream = text;

  // AVX2 offers broad host support with less throttling than AVX512, so this
  // path accepts its smaller shuffle lanes in exchange for predictable use.
  // Each lane has sixteen lookup entries for the five Base64 character groups.
  // Uppercase letters subtract 65, lowercase letters subtract 71, digits add
  // 4, plus adds 19, and slash adds 16.
  //
  // Inverting the high nibble and masking its top bit gives every group a
  // useful four bit shuffle index. The final nibble clears the shuffle control
  // bit at the same time, which saves an instruction and leaves enough room for
  // another unrolled round while the data dependency settles.
  //
  // This vector path expects validated Base64 input. Callers handling untrusted
  // text validate it before relying on the decoded bytes.
  const auto adjustment_values = _mm256_setr_epi8(
      // Lane 1
      /* 0 */ _,
      /* 'o' */ -71, _,
      /* 'O' */ -65, _,
      /* '/' */ +16, _, _,
      /* lowercase p through z */ -71,
      /* lowercase a through n */ -71,
      /* uppercase P through Z */ -65,
      /* uppercase A through N */ -65,
      /* digits */ +4,
      /* + */ +19, _, _,

      // Lane 2 repeats lane 1 because shuffles operate on independent 128 bit
      // lanes rather than the complete 256 bit AVX2 register.
      /* 0 */ _,
      /* 'o' */ -71, 0,
      /* 'O' */ -65, 0,
      /* '/' */ +16, _, 0,
      /* lowercase p through z */ -71,
      /* lowercase a through n */ -71,
      /* uppercase P through Z */ -65,
      /* uppercase A through N */ -65,
      /* digits */ +4,
      /* + */ +19, _, _);

  const auto invert_mask = _mm256_setr_epi8(
      // Lane 1 uses the final entry to remove the extra control bit.
      0b1111, 0b1111, 0b1111, 0b1111, 0b1111, 0b1111, 0b1111, 0b1111, 0b1111,
      0b1111, 0b1111, 0b1111, 0b1111, 0b1111, 0b1111, 0b0111,

      // Lane 2 repeats lane 1 because shuffles operate on independent 128 bit
      // lanes rather than the complete 256 bit AVX2 register.
      0b1111, 0b1111, 0b1111, 0b1111, 0b1111, 0b1111, 0b1111, 0b1111, 0b1111,
      0b1111, 0b1111, 0b1111, 0b1111, 0b1111, 0b1111, 0b0111);
  // Each lane packs twelve decoded bytes behind one unused dword. The dword
  // permutation joins both payloads before they are written.
  const auto repack_shuffle = _mm256_setr_epi8(
      // Lane 1
      0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x01, 0x00, 0x06, 0x05, 0x04, 0x0A, 0x09,
      0x08, 0x0E, 0x0D, 0x0C,

      // Lane 2
      0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x01, 0x00, 0x06, 0x05, 0x04, 0x0A, 0x09,
      0x08, 0x0E, 0x0D, 0x0C);
  const auto stream_order = _mm256_setr_epi32(1, 2, 3, 5, 6, 7, 0, 4);

  for (Count chunk = 0; chunk < vectorized_chunks; chunk++) {
    for (Count ymm = 0; ymm < fused_channels; ymm++) {
      const auto channel_value = _mm256_loadu_si256(
          Data::cast<const __m256i_u>(source_data + sizeof(__m256i) * ymm));

      const auto shift = _mm256_srli_epi64(channel_value, 4);
      const auto nibble_mask = _mm256_shuffle_epi8(invert_mask, channel_value);
      const auto adjusted_index = _mm256_andnot_si256(shift, nibble_mask);

      const auto adjust_value =
          _mm256_shuffle_epi8(adjustment_values, adjusted_index);
      const auto bit_value = _mm256_add_epi8(adjust_value, channel_value);

      // Lane merging:
      //
      // AAAAAA__ BBBBBB__ CCCCCC__ DDDDDD__
      // Multiply horizontally at 16 bit boundaries using 64 to shift 6 bits.
      // AAAAAA__ * 1  = AAAAAA__ ________
      // bbbbbb__ * 64 = ______bb bbbb____
      // Add multiplications:
      // AAAAAAbb bbbb____ CCCCCCdd dddd____
      const __m256i multiply_by_64 = _mm256_set_epi64x(
          0x0140014001400140, 0x0140014001400140, 0x0140014001400140,
          0x0140014001400140);
      const __m256i multiply_by_4096 = _mm256_set_epi64x(
          0x0001100000011000, 0x0001100000011000, 0x0001100000011000,
          0x0001100000011000);
      const auto half_merged = _mm256_maddubs_epi16(bit_value, multiply_by_64);

      // Merge adjacent 12 bit groups into 24 bit payloads. This completes each
      // three byte decode group before lane repacking.
      const auto merged_i32 = _mm256_madd_epi16(half_merged, multiply_by_4096);
      const auto lane_repack = _mm256_shuffle_epi8(merged_i32, repack_shuffle);

      // Compact the lane payloads so the eight unused bytes follow the decoded
      // output. The next store replaces that working space, while allocation
      // slack contains it after the final store.
      const auto stream_repack =
          _mm256_permutevar8x32_epi32(lane_repack, stream_order);
      _mm256_storeu_si256(
          (__m256i_u*)(output_stream + 24 * ymm), stream_repack);
    }

    output_stream +=
        (avx2_channel_width - avx2_channel_width / 4) * fused_channels;
    source_data += full_channel_width;
  }

#else

  constexpr Count source_vectorized_bytes = 0;
  auto output_stream = text;

#endif

  // Decode every quartet not consumed by a vector batch. On scalar targets the
  // vectorized byte count is zero, so this is the complete path.
  source_bytes -= source_vectorized_bytes;
  for (Count i = 0, j = 0; j < source_bytes;
       i += output_stride, j += source_stride) {
    output_stream[i] = (decode_lookup[source_data[j]] << 2) |
                       ((decode_lookup[source_data[j + 1]] & 0x30) >> 4);
    output_stream[i + 1] = ((decode_lookup[source_data[j + 1]] & 0x0f) << 4) |
                           ((decode_lookup[source_data[j + 2]] & 0x3c) >> 2);
    output_stream[i + 2] = ((decode_lookup[source_data[j + 2]] & 0x03) << 6) |
                           decode_lookup[source_data[j + 3]];
  }

  return size;
}

auto vectorize_encode(Access::Bytes output, View::Bytes source) -> void {

#ifdef PERI_AVX2

  // AMD processors without AVX512 can still use their additional AVX2 ports.
  // Unrolling two independent buffers also lets other modern processors overlap
  // the operations while retaining the broader AVX2 host support.
  //
  // Local testing found the gather and bitmask phases slower than native
  // AVX512. Avoiding the AVX512 power penalty still made this path faster on
  // current Intel processors.
  constexpr auto fused_channels = 2;
  constexpr auto avx2_channel_width = sizeof(__m256i);
  constexpr auto full_channel_width = avx2_channel_width * fused_channels;

#endif

  constexpr Count output_stride = 4;
  constexpr Count source_stride = 3;
  constexpr auto encode_filter = 64 - 1;

  auto source_data = source.get_data();
  auto source_bytes = source.get_size();
  const Count left_over = source_bytes % 3;
  auto output_stream = output.get_data();

#ifdef PERI_AVX2

  // Two scalar groups establish four readable bytes before the remaining View.
  // The vector loop can then load from that prefix without reading before the
  // caller supplied storage.
  if (source_bytes > full_channel_width * 2) {
    // AVX2 cannot shuffle values across its 128 bit lane boundary. Encoding two
    // scalar groups supplies the offset needed to place six complete groups
    // across that boundary.
    for (Count i = 0; i < 2; i++) {
      output_stream[0] = encode_lookup[(source_data[0] >> 2)];
      output_stream[1] = encode_lookup
          [((source_data[0] & 0b00000011) << 4) | (source_data[1] >> 4)];
      output_stream[2] = encode_lookup
          [((source_data[1] & 0b00001111) << 2) | (source_data[2] >> 6)];
      output_stream[3] = encode_lookup[source_data[2] & encode_filter];

      source_bytes -= 3;
      output_stream += output_stride;
      source_data += source_stride;
    }
  }

  // Encoding applies the inverse offsets to each six bit value. A saturating
  // subtraction by 51 folds the contiguous letter ranges into one shuffle
  // entry, while the comparison adds the smaller correction needed by the
  // lower letter range. Digits and the two punctuation values retain their
  // direct table entries.
  const auto adjustment_values = _mm256_setr_epi8(
      // Lane 1
      /* Alphas */ 71,
      /* '0' */ -4,
      /* '1' */ -4,
      /* '2' */ -4,
      /* '3' */ -4,
      /* '4' */ -4,
      /* '5' */ -4,
      /* '6' */ -4,
      /* '7' */ -4,
      /* '8' */ -4,
      /* '9' */ -4,
      /* '+' */ -19,
      /* '/' */ -16, _, _, _,

      // AVX2 shuffles operate independently within each 128 bit lane, so the
      // second lane repeats the first lane's mapping.
      /* Alphas */ 71,
      /* '0' */ -4,
      /* '1' */ -4,
      /* '2' */ -4,
      /* '3' */ -4,
      /* '4' */ -4,
      /* '5' */ -4,
      /* '6' */ -4,
      /* '7' */ -4,
      /* '8' */ -4,
      /* '9' */ -4,
      /* '+' */ -19,
      /* '/' */ -16, _, _, _);

  // Split each three byte group into four six bit values. The shuffle also
  // places bytes in little endian order and duplicates the shared middle byte.
  constexpr auto a_index = 0;
  constexpr auto b_index = 1;
  constexpr auto c_index = 1;
  constexpr auto d_index = 2;
  // `_mm256_set_epi8` lists groups from the highest output bytes down while
  // each group itself selects C, D, A, and B for the multiplication layout
  // below.
  const auto byte_shuffle = _mm256_set_epi8(
      25 + c_index, 25 + d_index, 25 + a_index, 25 + b_index, 22 + c_index,
      22 + d_index, 22 + a_index, 22 + b_index, 19 + c_index, 19 + d_index,
      19 + a_index, 19 + b_index, 16 + c_index, 16 + d_index, 16 + a_index,
      16 + b_index, 13 + c_index, 13 + d_index, 13 + a_index, 13 + b_index,
      10 + c_index, 10 + d_index, 10 + a_index, 10 + b_index, 7 + c_index,
      7 + d_index, 7 + a_index, 7 + b_index, 4 + c_index, 4 + d_index,
      4 + a_index, 4 + b_index);

  constexpr auto input_bytes_per_channel = sizeof(U32) * 6;
  constexpr auto input_bytes_per_iteration =
      input_bytes_per_channel * fused_channels;

  constexpr auto output_bytes_per_channel = sizeof(U32) * 8;
  constexpr auto output_bytes_per_iteration =
      output_bytes_per_channel * fused_channels;

  // Each channel load begins four bytes before its logical input and reads one
  // AVX2 register. The scalar prefix provides the leading bytes, while the loop
  // bound leaves four trailing bytes beyond the final complete iteration.
  while (source_bytes > input_bytes_per_iteration + 4) {
    for (auto ymm = fused_channels - 1; ymm >= 0; ymm--) {
      // Underreading by four bytes places six groups across the lane boundary.
      //
      //  32   32   32   32  |  32   32   32   32
      // ____ AAAB BBCC CDDD | EEEF FFGG GHHH ____
      const auto channel_value = _mm256_loadu_si256(
          Data::cast<const __m256i_u>(
              source_data - 4 + ymm * input_bytes_per_channel));

      const auto chunk_data = _mm256_shuffle_epi8(channel_value, byte_shuffle);

      // Isolate the bit fields used to form `c` and `a` from each shuffled four
      // byte group. The complementary mask below selects `d` and `b`.
      const auto c_and_a = _mm256_and_si256(
          chunk_data, _mm256_set1_epi32(0b00001111'11000000'11111100'00000000));
      const auto d_and_b = _mm256_and_si256(
          chunk_data, _mm256_set1_epi32(0b00000000'00111111'00000011'11110000));

      // Unsigned high multiplication moves C right by six bits and A right by
      // ten bits without requiring a variable shift instruction.
      const auto low_bytes = _mm256_mulhi_epu16(
          c_and_a, _mm256_set1_epi32(0b0000010000000000'0000000001000000));
      // Low multiplication moves B left by four bits and D left by eight bits
      // directly into their output positions.
      const auto high_bytes = _mm256_mullo_epi16(
          d_and_b, _mm256_set1_epi32(0b0000000100000000'0000000000010000));

      const auto final_bit_values = _mm256_or_si256(low_bytes, high_bytes);

      const auto shuffle_indexes =
          _mm256_subs_epu8(final_bit_values, _mm256_set1_epi8(51));
      const auto lower_alpha_mask =
          _mm256_cmpgt_epi8(_mm256_set1_epi8(26), final_bit_values);
      const auto lower_alpha_offset =
          _mm256_and_si256(_mm256_set1_epi8(-6), lower_alpha_mask);

      const auto offset_values =
          _mm256_shuffle_epi8(adjustment_values, shuffle_indexes);
      const auto final_values = _mm256_add_epi8(
          _mm256_add_epi8(final_bit_values, offset_values), lower_alpha_offset);

      _mm256_storeu_si256(
          Data::cast<__m256i_u>(output_stream + ymm * output_bytes_per_channel),
          final_values);
    }

    source_bytes -= input_bytes_per_iteration;
    source_data += input_bytes_per_iteration;
    output_stream += output_bytes_per_iteration;
  }

#endif

  // The inclusive comparison keeps the final complete three byte group from
  // not skipped when source size is an exact multiple of 3.
  while (source_bytes >= source_stride) {
    output_stream[0] = encode_lookup[(source_data[0] >> 2)];
    output_stream[1] = encode_lookup
        [((source_data[0] & 0b00000011) << 4) | (source_data[1] >> 4)];
    output_stream[2] = encode_lookup
        [((source_data[1] & 0b00001111) << 2) | (source_data[2] >> 6)];
    output_stream[3] = encode_lookup[source_data[2] & encode_filter];

    source_bytes -= 3;
    output_stream += output_stride;
    source_data += source_stride;
  }

  // Padding records values absent from the final three byte group. One
  // remaining byte therefore produces two symbols and two padding bytes. Two
  // remaining bytes produce three symbols and one padding byte.
  switch (left_over) {
  case 1:
    output_stream[0] = encode_lookup[(source_data[0] >> 2)];
    output_stream[1] = encode_lookup[((source_data[0] & 0b00000011) << 4)];
    output_stream[2] = '=';
    output_stream[3] = '=';
    break;
  case 2:
    output_stream[0] = encode_lookup[(source_data[0] >> 2)];
    output_stream[1] = encode_lookup
        [((source_data[0] & 0b00000011) << 4) | (source_data[1] >> 4)];
    output_stream[2] = encode_lookup[((source_data[1] & 0b00001111) << 2)];
    output_stream[3] = '=';
    break;
  default:
    break;
  }
}

auto Encodings::Base64::decode(View::Bytes source)
    -> Storage::Option<Thread::Bytes> {
  if (!validate_decode(source)) {
    return Storage::Option<Thread::Bytes>();
  }

  if (source.is_empty()) {
    return Thread::Bytes();
  }

  Count size = (source.get_size() / 4) * 3;

  // Reserve the worst case decoded size plus tail scratch. The final resize
  // removes padding bytes after vector and scalar writes finish.
  Memory::Thread::Bytes bytes;
  bytes.forgetful_resize(size + decode_extra_bytes);

  U8* text = bytes.get_access().get_data();
  Count actual_size = vectorized_decode(text, source);
  bytes.resize(actual_size);
  return bytes;
}

auto Encodings::Base64::decode(Allocator::Arena& arena, View::Bytes source)
    -> Storage::Option<View::Bytes> {
  if (!validate_decode(source)) {
    return Storage::Option<View::Bytes>();
  }

  if (source.is_empty()) {
    return View::Bytes();
  }

  Count size = (source.get_size() / 4) * 3;

  auto storage = arena.allocate(size + decode_extra_bytes);
  U8* text = storage.get_data();
  Count actual_size = vectorized_decode(text, source);
  if (actual_size == 0) {
    return View::Bytes();
  }

  return View::Bytes(text[0], actual_size);
}

auto Encodings::Base64::encode(View::Bytes source) -> Thread::Bytes {
  if (source.is_empty()) {
    return Thread::Bytes();
  }

  const Count left_over = source.get_size() % 3;
  const Count output_size = (source.get_size() / 3) * 4 + (left_over ? 4 : 0);
  Thread::Bytes bytes;
  bytes.forgetful_resize(output_size);
  vectorize_encode(bytes.get_access(), source);
  return bytes;
}

auto Encodings::Base64::encode(Allocator::Arena& arena, View::Bytes source)
    -> View::Bytes {
  if (source.is_empty()) {
    return View::Bytes();
  }

  const Count left_over = source.get_size() % 3;
  const Count output_size = (source.get_size() / 3) * 4 + (left_over ? 4 : 0);
  Access::Bytes output = arena.allocate(output_size);
  vectorize_encode(output, source);
  return output;
}
