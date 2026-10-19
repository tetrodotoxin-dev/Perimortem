// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/view/bytes.hpp"

namespace Perimortem::Core::Reader {

// Serial decodes signed integers and byte blobs from Perimortem's schemaless
// Serial format without allocating. Each field begins with one type byte whose
// low nibble selects a one, two, four or eight byte little endian magnitude.
// Separate flags distinguish negative integers and blob byte counts.
//
// A successful read advances past the complete field. Malformed or truncated
// input sets the cursor to `Count(-1)`. Later reads return zero initialized
// values without accessing the source until `reset`. A copy receives the same
// source and cursor, then advances independently.
class Serial {
 public:
  // Value stores either one decoded integer or a View into the Serial source.
  // Blob bytes are not copied, so the caller keeps the source alive while using
  // the returned View. `is_blob` distinguishes an empty blob from integer zero.
  // A default Value has the empty blob representation. The Reader cursor
  // distinguishes that zero state after decode failure from a successfully
  // decoded empty blob.
  class Value {
   public:
    constexpr Value() : type_info(0), value(0) {}
    constexpr Value(S64 value) : type_info(0x8000000000000000), value(value) {}
    constexpr Value(View::Bytes view)
        : type_info(view.get_size()), blob(view.get_data()) {}

    constexpr operator View::Bytes() { return get_view(); }
    constexpr operator S64() { return get_value(); }

    constexpr auto get_view() -> View::Bytes {
      if ((type_info & 0x8000000000000000) || type_info == 0) {
        return View::Bytes();
      }

      return View::Bytes(blob[0], type_info);
    }

    constexpr auto get_value() -> S64 {
      if (type_info & 0x8000000000000000) {
        return value;
      }

      return S64();
    }

    constexpr auto is_blob() -> Bool {
      return !(type_info & 0x8000000000000000);
    }

   private:
    // Value keeps its blob/value discriminator in type_info so the complete
    // schemaless read remains 16 bytes. Storage::Union would require a separate
    // tag and grow this hot return type to 24 bytes.
    U64 type_info;
    union {
      const U8* blob;
      S64 value;
    };
  };

  constexpr Serial(View::Bytes source) : source(source) {}
  constexpr Serial(const Serial& rhs)
      : source(rhs.source), cursor(rhs.cursor) {}

  // Selects the next byte to decode. A location at the source size represents
  // ordinary exhaustion. A location beyond it causes the next read to report
  // an overrun and enter the invalid state.
  constexpr auto set_location(Count location) -> void { cursor = location; }
  constexpr auto get_location() const -> Count { return cursor; }

  // Decodes the next integer or blob and advances past its complete encoding.
  // Malformed or truncated input invalidates the reader and returns the default
  // Value. That value reports an empty blob and converts to integer zero.
  EXPORTED(PERIMORTEM) auto read() -> Value;

  // Decodes the next field and returns its integer value. A blob still advances
  // the cursor but returns zero. Decode failure also returns zero and
  // invalidates the reader.
  EXPORTED(PERIMORTEM) auto read_value() -> S64;

  // Decodes the next field and returns a View into its blob bytes. An integer
  // still advances the cursor but returns an empty View. Decode failure also
  // returns an empty View and invalidates the reader.
  EXPORTED(PERIMORTEM) auto read_blob() -> View::Bytes;

  constexpr auto get_size() const -> Count { return source.get_size(); }
  constexpr auto has_content() const -> Bool {
    return cursor < source.get_size();
  }

  constexpr auto reset() -> void { cursor = 0; }

 private:
  View::Bytes source{};
  Count cursor = 0;
};

}  // namespace Perimortem::Core::Reader
