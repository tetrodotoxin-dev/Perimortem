// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/view/bytes.hpp"

namespace Perimortem::Core::Reader {

// Textual decodes bytes, flags and numbers in fixed notation from one read only
// byte range without allocating. Numeric and flag reads skip JSON whitespace,
// while `read_byte` consumes the next byte exactly. Each token consumes its
// longest accepted prefix, so adjacent numeric fields require a separator byte
// that is not a digit.
//
// Flags accept `true` and `false` with either case for the first letter.
// Unsigned integers accept radices from two through sixteen. Signed integers
// and Reals use decimal with an optional minus sign. Reals do not accept an
// exponent.
//
// A failed parse or numeric conversion outside the return type's supported
// range sets the cursor to `Count(-1)` and returns zero. Later reads return
// zero without accessing the source until `reset`. A copy receives the same
// source and cursor, then advances independently.
class Textual {
 public:
  constexpr Textual(View::Bytes source) : source(source) {}
  constexpr Textual(const Textual& rhs)
      : source(rhs.source), cursor(rhs.cursor) {}

  // Selects the next byte to decode. The source size represents ordinary
  // exhaustion. A larger location enters the invalid state.
  constexpr auto set_location(Count location) -> void {
    cursor = location <= source.get_size() ? location : Count(-1);
  }

  constexpr auto get_location() const -> Count { return cursor; }

  EXPORTED(PERIMORTEM) auto read_byte() -> U8;
  EXPORTED(PERIMORTEM) auto read_flag() -> Bool;
  // Reads one unsigned value using a radix from 2 through 16.
  EXPORTED(PERIMORTEM) auto read_unsigned(U8 radix = 10) -> U64;
  EXPORTED(PERIMORTEM) auto read_signed() -> S64;
  EXPORTED(PERIMORTEM) auto read_r32() -> R32;
  EXPORTED(PERIMORTEM) auto read_r64() -> R64;

  constexpr auto get_size() const -> Count { return source.get_size(); }
  constexpr auto is_valid() const -> Bool { return cursor != Count(-1); }
  constexpr auto has_content() const -> Bool {
    return cursor < source.get_size();
  }

  constexpr auto reset() -> void { cursor = 0; }

 private:
  View::Bytes source{};
  Count cursor = 0;
};

}  // namespace Perimortem::Core::Reader
