// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.hpp"

#include "perimortem/core/identity/uuid.hpp"
#include "perimortem/core/storage/bytes.hpp"

namespace Perimortem::Serialization::Formats {

// Uuid converts Core::Identity::Uuid values to and from hexadecimal text.
// The high word supplies the first sixteen digits and the low word supplies
// the final sixteen. Conversion uses fixed storage without allocating.
//
// Serialization always emits 36 lowercase bytes in the dashed 8-4-4-4-12
// form. Deserialization accepts exactly 32 packed or 36 dashed hexadecimal
// bytes with either letter case. Malformed text produces the zero UUID. Since
// the zero UUID is also valid text, callers that must distinguish malformed
// input use `is_valid` before deserialization. Validation is lexical and does
// not require particular UUID version or variant bits.
class Uuid {
  static constexpr auto ascii_to_nibble(U8 byte) -> U64 {
    switch (byte) {
    case '0' ... '9':
      return byte - '0';
    case 'a' ... 'f':
      return byte - 'a' + 10;
    case 'A' ... 'F':
      return byte - 'A' + 10;
    default:
      return 0;
    }
  }

 public:
  // Validates and reads packed hexadecimal with the first sixteen digits in
  // the high word. Invalid input produces the zero UUID.
  static constexpr auto deserialize(const Core::Storage::Bytes<32>& source)
      -> Core::Identity::Uuid {
    if !consteval {
      return parse(source);
    }

    if (!is_valid(source)) {
      return Core::Identity::Uuid();
    }

    perimortem_uuid result{};
    for (Count index = 0; index < 16; index++) {
      result.high |= ascii_to_nibble(source[index]) << (60 - index * 4);
    }

    for (Count index = 0; index < 16; index++) {
      result.low |= ascii_to_nibble(source[index + 16]) << (60 - index * 4);
    }

    return Core::Identity::Uuid(result);
  }

  // Validates and reads dashed hexadecimal with the first sixteen digits in
  // the high word. Invalid input produces the zero UUID.
  static constexpr auto deserialize(const Core::Storage::Bytes<36>& source)
      -> Core::Identity::Uuid {
    if !consteval {
      return parse(source);
    }

    if (!is_valid(source)) {
      return Core::Identity::Uuid();
    }

    perimortem_uuid result{};
    for (Count index = 0; index < 8; index++) {
      result.high |= ascii_to_nibble(source[index]) << (60 - index * 4);
    }

    for (Count index = 0; index < 4; index++) {
      result.high |= ascii_to_nibble(source[index + 9]) << (28 - index * 4);
    }

    for (Count index = 0; index < 4; index++) {
      result.high |= ascii_to_nibble(source[index + 14]) << (12 - index * 4);
    }

    for (Count index = 0; index < 4; index++) {
      result.low |= ascii_to_nibble(source[index + 19]) << (60 - index * 4);
    }

    for (Count index = 0; index < 12; index++) {
      result.low |= ascii_to_nibble(source[index + 24]) << (44 - index * 4);
    }

    return Core::Identity::Uuid(result);
  }

  // Reports whether a View contains one complete packed or dashed UUID. Both
  // hexadecimal letter cases are accepted. Dashed input separates groups of
  // eight, four, four, four and twelve digits.
  static constexpr auto is_valid(Core::View::Bytes source) -> Bool {
    if consteval {
      const auto is_hex = [](U8 byte) -> Bool {
        return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') ||
               (byte >= 'A' && byte <= 'F');
      };

      if (source.get_size() == 32) {
        for (Count index = 0; index < source.get_size(); index++) {
          if (!is_hex(source[index])) {
            return False;
          }
        }

        return True;
      }

      if (source.get_size() == 36) {
        for (Count index = 0; index < source.get_size(); index++) {
          const Bool separator =
              index == 8 || index == 13 || index == 18 || index == 23;
          if (separator ? source[index] != '-' : !is_hex(source[index])) {
            return False;
          }
        }

        return True;
      }

      return False;
    } else {
      return validate(source);
    }
  }

  // Returns exactly 36 lowercase characters with all four separators. The
  // returned storage contains the complete text without a null terminator.
  static EXPORTED(PERIMORTEM) auto serialize(Core::Identity::Uuid source)
      -> Core::Storage::Bytes<36>;

 private:
  // Runtime operations select the target implementation while their public
  // constexpr entry points retain constant evaluation in this header.
  static EXPORTED(PERIMORTEM) auto validate(Core::View::Bytes source) -> Bool;
  static EXPORTED(PERIMORTEM) auto parse(const Core::Storage::Bytes<32>& source)
      -> Core::Identity::Uuid;
  static EXPORTED(PERIMORTEM) auto parse(const Core::Storage::Bytes<36>& source)
      -> Core::Identity::Uuid;
};

}  // namespace Perimortem::Serialization::Formats
