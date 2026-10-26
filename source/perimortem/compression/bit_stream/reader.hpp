// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#define TOOLCHAIN_PRIVATE_HEADER 1

#include "perimortem/core/view/bytes.hpp"

namespace Perimortem::Compression::BitStream {

// Reader decodes one DEFLATE packed View inside Deflate without allocating.
// Each byte is consumed from its least significant bit, while Huffman codes
// accumulate from their most significant bit to preserve canonical code order.
//
// A 64 bit buffer keeps common peeks and advances independent of another source
// load. Reading beyond the View or requesting a code wider than U32 makes the
// invalid state sticky. Values returned by later reads are undefined, while the
// operations remain within the source View.
class Reader {
 public:
  constexpr Reader(Core::View::Bytes source) : data(source) {}

  // Consumes one bit. Exhaustion returns False and invalidates the Reader, so
  // `is_valid()` distinguishes that failure from a decoded zero bit.
  auto read_bit() -> Bool;

  // Consumes at most 32 low order stream bits. A wider or truncated request
  // returns zero and invalidates the Reader.
  auto read_code(Count count) -> U32;

  // Discards a partial byte, then returns the requested complete bytes as a
  // View into the source. Insufficient input returns an empty View and
  // invalidates the Reader.
  auto read_raw_bytes(Count count) -> Core::View::Bytes;

  // Returns at most 32 low order stream bits without advancing. Missing high
  // bits remain zero so a Huffman decoder can recognize a shorter complete
  // code. Its later advance determines whether enough bits were available.
  auto peek_code(Count count) -> U32;

  // Advances only through bits already loaded into the buffer. A request past
  // that boundary invalidates the stream without underflowing `buffered_bits`.
  constexpr auto advance_bits(Count count) -> Bool {
    if (count > buffered_bits) [[unlikely]] {
      invalid_stream = True;
      return False;
    }

    buffer = count == 64 ? 0 : buffer >> count;
    buffered_bits -= count;
    return True;
  }

  constexpr auto is_valid() const -> Bool { return !invalid_stream; }
  constexpr auto invalidate() -> void { invalid_stream = True; }

 private:
  auto fill() -> void;

  Core::View::Bytes data{};
  U64 buffer = 0;
  Count buffered_bits = 0;
  Count byte_position = 0;
  Bool invalid_stream = False;
};

}  // namespace Perimortem::Compression::BitStream
