// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/storage/option.hpp"
#include "perimortem/core/view/bytes.h"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/thread/bytes.hpp"

namespace Perimortem::Serialization::Encodings {

// Base64 converts arbitrary bytes to and from the padded RFC 4648 encoding.
// Encoded text contains complete four byte groups drawn from A-Z, a-z, 0-9,
// `+` and `/`. One or two `=` bytes may complete only the final group, and bits
// hidden by that padding are zero. These rules give each byte sequence one
// encoded representation.
//
// Encoding is total for every byte sequence. Decoding distinguishes valid
// empty input from malformed text and allocates only after validating the
// complete source.
class Base64 {
 public:
  // Decoding requires complete four character groups and canonical trailing
  // padding. Malformed input returns None without allocating. An empty source
  // succeeds with selected, allocation free empty bytes.
  static EXPORTED(PERIMORTEM) auto decode(Core::View::Bytes source)
      -> Core::Storage::Option<Memory::Thread::Bytes>;

  // The Arena overload applies the same validation and returns a View over its
  // decoded allocation. The View remains accessible until the Arena resets or
  // is destroyed.
  static EXPORTED(PERIMORTEM) auto decode(
      Memory::Allocator::Arena& arena,
      Core::View::Bytes source) -> Core::Storage::Option<Core::View::Bytes>;

  // Encodes every source byte and supplies canonical '=' padding for the final
  // partial group. Empty input returns allocation free empty bytes.
  static EXPORTED(PERIMORTEM) auto encode(Core::View::Bytes source)
      -> Memory::Thread::Bytes;

  // The Arena overload returns a View over its encoded allocation. The View
  // remains accessible until the Arena resets or is destroyed.
  static EXPORTED(PERIMORTEM) auto encode(
      Memory::Allocator::Arena& arena,
      Core::View::Bytes source) -> Core::View::Bytes;
};

}  // namespace Perimortem::Serialization::Encodings
