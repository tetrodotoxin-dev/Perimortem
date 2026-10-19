// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/view/bytes.hpp"
#include "perimortem/memory/thread/vector.hpp"

namespace Perimortem::Compression {

// Lz77 maintains a sliding hash chain from each three byte prefix to earlier
// source positions. Match selection walks recent candidates within the Deflate
// window and caller supplied depth, preserving enough history for either
// immediate emission or speculative look ahead.
//
// Fixed hash and chain tables avoid `Thread::Map` bookkeeping in the Deflate
// hot path, where the general map produced lower measured throughput.
class Lz77 {
 public:
  static constexpr Count min_match = 3;
  static constexpr Count max_match = 258;

  class Match {
   public:
    Match() : length(0), distance(0) {}
    Match(Count length, Count distance) : length(length), distance(distance) {}

    auto get_length() const -> Count { return length; }
    auto get_distance() const -> Count { return distance; }

   private:
    Count length;
    Count distance;
  };

  EXPORTED(PERIMORTEM) Lz77();

  EXPORTED(PERIMORTEM) auto reset() -> void;
  EXPORTED(PERIMORTEM) auto find_match(
      Core::View::Bytes source,
      Count position,
      Count depth) const -> Match;
  EXPORTED(PERIMORTEM) auto insert(Core::View::Bytes source, Count position)
      -> void;
  // Inserts the position and searches from the previous chain head in one pass.
  // Starting from that old head prevents the new entry from matching itself.
  EXPORTED(PERIMORTEM) auto find_match_and_insert(
      Core::View::Bytes source,
      Count position,
      Count depth) -> Match;

 private:
  Memory::Thread::Vector<U32> hash_table;
  Memory::Thread::Vector<U32> chain_table;
};

}  // namespace Perimortem::Compression
