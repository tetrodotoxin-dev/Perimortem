// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/access/bytes.hpp"
#include "perimortem/core/data.hpp"
#include "perimortem/core/view/bytes.hpp"
#include "perimortem/core/view/vector.hpp"

namespace Perimortem::Core::Writer {

// Serial writes signed integers and byte blobs into a caller supplied mutable
// range using Perimortem's schemaless Serial format. Integers use one type byte
// followed by the narrowest one, two, four or eight byte magnitude. Negative
// values set the sign flag without widening that magnitude. Multi-byte
// magnitudes use little endian order. Blobs set the blob flag on an encoded
// byte count and append their bytes directly afterward.
//
// A write that cannot fit leaves the cursor and destination unchanged, then
// marks the writer invalid. Later writes remain memory safe, but their cursor
// and output effects are undefined. A copy receives the same cursor and
// validity state, then advances independently over the shared destination so
// writes from either Writer can overlap.
class Serial {
 public:
  constexpr Serial(Access::Bytes source) : source(source) {};
  constexpr Serial(const Serial& rhs)
      : source(rhs.source), cursor(rhs.cursor), valid_state(rhs.valid_state) {};

  // Sets the cursor to the requested location or the end of the buffer when the
  // requested location lies beyond its writable storage.
  EXPORTED(PERIMORTEM) auto set_pointer(Count location) -> void;

  EXPORTED(PERIMORTEM) auto operator<<(const S64 value) -> Serial&;
  EXPORTED(PERIMORTEM) auto operator<<(const View::Bytes blob) -> Serial&;

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
  Access::Bytes source;
  Count cursor = 0;
  Bool valid_state = True;
};

}  // namespace Perimortem::Core::Writer
