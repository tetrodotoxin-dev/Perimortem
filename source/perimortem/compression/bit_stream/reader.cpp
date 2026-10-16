// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/compression/bit_stream/reader.hpp"

#include "perimortem/core/data.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Compression;

constexpr auto code_mask(Count count) -> U32 {
  return count == 32 ? U32(-1) : (U32(1) << count) - U32(1);
}

// Loads bytes from the stream into buffer until it buffers at least 57 bits or
// the stream is exhausted. 57 bits guarantees a 9 bit peek after consuming up
// to a 48 bit code + extra bits pair without a second fill.
auto BitStream::Reader::fill() -> void {
  const Count size = data.get_size();
  const U8* source_pointer = data.get_data();

  // Fast path: enough bytes remain for one packed 64 bit read. DEFLATE stores
  // the least significant stream byte first, so the declared byte order keeps
  // the accumulator identical on every host.
  if (byte_position <= size && sizeof(U64) <= size - byte_position) {
    U64 word = Data::read<Data::ByteOrder::Little, U64>(
        source_pointer + byte_position);
    Count consumed = (64 - buffered_bits) >> 3;
    buffer |= word << buffered_bits;
    byte_position += consumed;
    buffered_bits += consumed * 8;
    return;
  }

  // Fewer than eight source bytes remain, so a packed U64 read would cross the
  // source View boundary. Individual byte loads fill the remaining buffer
  // without reading outside the supplied storage.
  while (buffered_bits <= 56 && byte_position < size) {
    buffer |= U64(source_pointer[byte_position++]) << buffered_bits;
    buffered_bits += 8;
  }
}

auto BitStream::Reader::read_bit() -> Bool {
  if (buffered_bits == 0) {
    fill();
    if (buffered_bits == 0) [[unlikely]] {
      invalid_stream = True;
      return False;
    }
  }

  const Bool bit = buffer & U64(1);
  advance_bits(1);
  return bit;
}

auto BitStream::Reader::read_code(Count count) -> U32 {
  if (count > 32) [[unlikely]] {
    invalid_stream = True;
    return 0;
  }

  if (buffered_bits < count) {
    fill();
    if (buffered_bits < count) [[unlikely]] {
      invalid_stream = True;
      return 0;
    }
  }

  const U32 result = U32(buffer) & code_mask(count);
  advance_bits(count);
  return result;
}

auto BitStream::Reader::read_raw_bytes(Count count) -> View::Bytes {
  // Raw blocks begin at the next byte boundary. Fractional buffered bits are
  // discarded. Complete buffered bytes were prefetched from the source, so
  // moving `byte_position` backward makes them part of the returned View.
  const Count fractional_bits = buffered_bits & 7;
  advance_bits(fractional_bits);

  const Count buffered_bytes = buffered_bits >> 3;
  if (buffered_bytes > byte_position) [[unlikely]] {
    invalid_stream = True;
    return View::Bytes();
  }

  byte_position -= buffered_bytes;
  buffer = 0;
  buffered_bits = 0;
  if (byte_position > data.get_size() ||
      count > data.get_size() - byte_position) [[unlikely]] {
    invalid_stream = True;
    return View::Bytes();
  }

  auto result = data.slice(byte_position, count);
  byte_position += count;
  return result;
}

auto BitStream::Reader::peek_code(Count count) -> U32 {
  if (count > 32) [[unlikely]] {
    invalid_stream = True;
    return 0;
  }

  if (buffered_bits < count) {
    fill();
  }

  return U32(buffer) & code_mask(count);
}
