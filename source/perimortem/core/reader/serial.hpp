// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/view/bytes.hpp"

namespace Perimortem::Core::Reader {

// Reads values from bytes in the Perimortem Serial format.
//
// Values are encoded as 1 type byte along with their minimum byte form and are
// always returned as 64 bit values regardless of how they were encoded.
//
// Blobs are encoded as a Value + a string of raw bytes. The Value's type byte
// contains an additional flag encoding that it is a blob.
//
// Negative values store an unsigned magnitude and mark their type byte with the
// negate flag. Reading reconstructs the signed value after decoding its width.
//
// On any error the reader enters an invalid state and all subsequent reads
// return zero initialized values without advancing the cursor.
class Serial {
 public:
  // Value stores either one decoded integer or a View into the Serial source.
  // Blob bytes are not copied, so the caller keeps the source alive while using
  // the returned View. `is_blob` distinguishes an empty blob from integer zero.
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

  // Sets the location of the read cursor.
  //
  // An out of range location invalidates the reader by setting the position to
  // the maximum Count value. Converting negative one to Count offers callers a
  // convenient spelling for that invalid state.
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
