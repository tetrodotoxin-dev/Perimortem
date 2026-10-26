// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#define TOOLCHAIN_PRIVATE_HEADER 1

#include "perimortem/memory/thread/bytes.hpp"

namespace Perimortem::Compression::BitStream {

// Writer appends the Deflate packed stream to caller supplied Thread Bytes. The
// output can grow during encoding, unlike the fixed Access range consumed by
// Core Writer.
//
// Each byte is filled starting from its least significant bit, and Huffman
// codes are written from the most significant bit to match canonical decode
// order.
//
// A 64 bit accumulator buffers bits until complete bytes can be appended.
// Deflate supplies no more than the 32 bits represented by each U32 argument.
// Keeping that bound at the call sites avoids another branch for every encoded
// field. Huffman::encode_symbol returns reversed stream bits, making
// `write_code` a direct alias for `write_bits`.
class Writer {
 public:
  Writer(Memory::Thread::Bytes& output) : output(output) {}

  // Writes at most 32 low order bits of `value` least significant bit first.
  // Extra bits and block headers use this ordering directly.
  auto write_bits(U32 value, Count length) -> void;
  auto write_code(U32 code, Count length) -> void;
  auto flush() -> void;

 private:
  auto drain() -> void;

  Memory::Thread::Bytes& output;
  U64 accumulator = 0;
  Count bits = 0;
};

}  // namespace Perimortem::Compression::BitStream
