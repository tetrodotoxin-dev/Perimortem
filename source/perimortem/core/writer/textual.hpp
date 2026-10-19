// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/access/bytes.hpp"
#include "perimortem/core/view/bytes.hpp"

namespace Perimortem::Core::Writer {

// Textual writes primitive values into a caller supplied mutable byte range
// without allocating or appending a null terminator. Each write begins at the
// current cursor. Boolean values use lowercase `true` and `false`, integers use
// decimal, Reals use fixed notation, and characters and byte Views append
// unchanged. Conversion to View exposes exactly the prefix through that cursor.
//
// A write that cannot fit marks the writer invalid. Integer and text writes
// reserve their complete output before changing storage. Real formatting can
// leave a partial prefix. Later writes remain memory safe, but their cursor and
// output effects are undefined. A copy receives the same cursor and validity
// state, then advances independently over the shared destination so writes from
// either Writer can overlap.
//
// Real formatting accepts finite values whose integer portion fits S64. Other
// values mark the writer invalid. Supported values use fixed notation with at
// most 32 characters and stop at the precision of their source type. This is a
// bounded readable representation rather than a shortest or exact round trip
// decimal.
class Textual {
 public:
  Textual(Core::Access::Bytes source) : source(source) {};
  Textual(const Textual& rhs)
      : source(rhs.source), cursor(rhs.cursor), valid_state(rhs.valid_state) {};

  // Moves the write cursor to `location`, clamped to the buffer size. Existing
  // values and the current validity state remain unchanged.
  EXPORTED(PERIMORTEM) auto set_pointer(Count location) -> void;

  EXPORTED(PERIMORTEM) auto operator<<(char character) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(const Bool flag) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(const U8 byte) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(const U16 value) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(const U32 value) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(const U64 value) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(const S8 value) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(const S16 value) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(const S32 value) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(const S64 value) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(const R32 value) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(const R64 value) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(const View::Bytes raw) -> Textual&;

  // C strings do not carry an extent and would make a bounded write depend on a
  // terminator search. Callers provide View::Bytes so embedded nulls and the
  // exact value count remain explicit.
  auto operator<<(const char* raw) -> Textual& = delete;

  constexpr auto get_size() const -> Count { return source.get_size(); }
  constexpr auto get_location() const -> Count { return cursor; }
  constexpr auto is_valid() const -> Bool { return valid_state; }
  constexpr operator View::Bytes() const {
    if (cursor == 0) {
      return View::Bytes();
    }

    return View::Bytes(source.get_data()[0], cursor);
  }

 private:
  auto write_real(R64 real, R64 precision) -> void;
  Access::Bytes source;
  Count cursor = 0;
  Bool valid_state = true;
};

}  // namespace Perimortem::Core::Writer
