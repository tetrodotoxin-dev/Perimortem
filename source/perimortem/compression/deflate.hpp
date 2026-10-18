// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/storage/option.hpp"
#include "perimortem/memory/thread/bytes.hpp"

namespace Perimortem::Compression {

// Deflate converts arbitrary bytes to and from zlib wrapped DEFLATE streams.
// Compression accepts every byte sequence, including an empty sequence.
// Inflation selects Bytes only after validating the wrapper and complete block
// structure, allowing a valid empty payload to remain distinct from malformed
// input.
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

  // Decompresses a zlib wrapped DEFLATE stream and validates its Adler32
  // trailer. A valid stream selects its complete payload, including selected
  // allocation free empty Bytes. Malformed input or checksum failure returns
  // None after logging the rejected stream condition.
  //
  // `capacity_hint` reserves output storage when the decompressed size is known
  // ahead of time, eliminating reallocation cascades on large inputs without
  // changing the accepted stream extent.
  static EXPORTED(PERIMORTEM) auto inflate(
      Core::View::Bytes source,
      Count capacity_hint = 0) -> Core::Storage::Option<Memory::Thread::Bytes>;

  // Applies the same structural validation as `inflate` without computing
  // Adler32 over the decompressed payload. Callers explicitly accept undetected
  // payload corruption in exchange for avoiding that additional pass.
  static EXPORTED(PERIMORTEM) auto inflate_without_checksum(
      Core::View::Bytes source,
      Count capacity_hint = 0) -> Core::Storage::Option<Memory::Thread::Bytes>;

  // Compresses every source value into a zlib wrapped DEFLATE stream. Even an
  // empty source produces the required wrapper and final block. `Level::None`
  // stores raw blocks while higher levels apply LZ77 and Huffman coding.
  static EXPORTED(PERIMORTEM) auto deflate(
      Core::View::Bytes source,
      Level level = Level::Default) -> Memory::Thread::Bytes;
};

}  // namespace Perimortem::Compression
