// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/memory/thread/bytes.hpp"

namespace Perimortem::Compression {

class Deflate {
 public:
  // None writes stored blocks without LZ77 or Huffman coding. Default examines
  // up to eight hash chain candidates for each match. Best examines up to 128,
  // spending more compression time to search for longer matches.
  enum class Level : U8 {
    None = 0,
    Default = 1,
    Best = 2,
  };

  // Decompresses a zlib wrapped Deflate stream and validates its Adler32
  // trailer. Returns empty bytes on malformed input or checksum failure.
  // `capacity_hint` reserves in advance the output buffer when the decompressed
  // size is known ahead of time, eliminating reallocation cascades on large
  // inputs.
  static EXPORTED(PERIMORTEM) auto inflate(
      Core::View::Bytes source,
      Count capacity_hint = 0) -> Memory::Thread::Bytes;

  // Applies the same structural validation as `inflate` without computing
  // Adler32 over the decompressed payload. Callers explicitly accept undetected
  // payload corruption in exchange for avoiding that additional pass.
  static EXPORTED(PERIMORTEM) auto inflate_without_checksum(
      Core::View::Bytes source,
      Count capacity_hint = 0) -> Memory::Thread::Bytes;

  // Compresses data to a deflate stream (RFC 1950 + RFC 1951).
  // Level::None stores raw blocks. Higher levels apply LZ77 + Huffman coding.
  static EXPORTED(PERIMORTEM) auto deflate(
      Core::View::Bytes source,
      Level level = Level::Default) -> Memory::Thread::Bytes;
};

}  // namespace Perimortem::Compression
