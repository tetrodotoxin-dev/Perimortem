// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.hpp"

#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/serialization/formats/uuid.h"

namespace Perimortem::Serialization::Formats {

// Uuid provides C++ comparison and text conversion for one 128 bit
// `perimortem_uuid`. The high word contains the first sixteen hexadecimal
// digits and the low word contains the final sixteen. Default construction
// produces the zero UUID.
//
// Serialization always emits 36 lowercase bytes in the dashed 8-4-4-4-12
// form. Deserialization accepts exactly 32 packed or 36 dashed hexadecimal
// bytes with either letter case. Malformed text produces the zero UUID. Since
// the zero UUID is also valid text, callers that must distinguish malformed
// input use `is_valid` before deserialization. Validation is lexical and does
// not require particular UUID version or variant bits.
//
// Uuid stores the C record directly so both language interfaces exchange the
// same two word value without C++ lifetime work.
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
  constexpr Uuid() = default;
  constexpr Uuid(const Uuid&) = default;
  constexpr Uuid(perimortem_uuid value) : value(value) {}

  // Validates and reads packed hexadecimal with the first sixteen digits in
  // the high word. Invalid input produces the zero UUID.
  static constexpr auto deserialize(const Core::Storage::Bytes<32>& source)
      -> Uuid {
    if !consteval {
      return parse(source);
    }

    if (!is_valid(source)) {
      return Uuid();
    }

    Uuid result;
    for (Count index = 0; index < 16; index++) {
      result.value.high |= ascii_to_nibble(source[index]) << (60 - index * 4);
    }

    for (Count index = 0; index < 16; index++) {
      result.value.low |= ascii_to_nibble(source[index + 16])
                          << (60 - index * 4);
    }

    return result;
  }

  // Validates and reads dashed hexadecimal with the first sixteen digits in
  // the high word. Invalid input produces the zero UUID.
  static constexpr auto deserialize(const Core::Storage::Bytes<36>& source)
      -> Uuid {
    if !consteval {
      return parse(source);
    }

    if (!is_valid(source)) {
      return Uuid();
    }

    Uuid result;
    for (Count index = 0; index < 8; index++) {
      result.value.high |= ascii_to_nibble(source[index]) << (60 - index * 4);
    }

    for (Count index = 0; index < 4; index++) {
      result.value.high |= ascii_to_nibble(source[index + 9])
                           << (28 - index * 4);
    }

    for (Count index = 0; index < 4; index++) {
      result.value.high |= ascii_to_nibble(source[index + 14])
                           << (12 - index * 4);
    }

    for (Count index = 0; index < 4; index++) {
      result.value.low |= ascii_to_nibble(source[index + 19])
                          << (60 - index * 4);
    }

    for (Count index = 0; index < 12; index++) {
      result.value.low |= ascii_to_nibble(source[index + 24])
                          << (44 - index * 4);
    }

    return result;
  }

  constexpr Uuid(U64 high, U64 low) : value{high, low} {}

  constexpr operator perimortem_uuid() const { return value; }

  constexpr auto operator==(const Uuid& other) const -> Bool {
    return value.high == other.value.high && value.low == other.value.low;
  }

  constexpr auto operator!=(const Uuid& other) const -> Bool {
    return value.high != other.value.high || value.low != other.value.low;
  }

  constexpr auto operator<(const Uuid& other) const -> Bool {
    return value.high == other.value.high ? value.low < other.value.low
                                          : value.high < other.value.high;
  }

  constexpr auto get_value() const -> perimortem_uuid { return value; }

  constexpr auto is_set() const -> Bool {
    return value.high != 0 || value.low != 0;
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

  EXPORTED(PERIMORTEM) auto serialize() const -> const Core::Storage::Bytes<36>;

 private:
  // Runtime operations select the target implementation while their public
  // constexpr entry points retain constant evaluation in this header.
  static EXPORTED(PERIMORTEM) auto validate(Core::View::Bytes source) -> Bool;
  static EXPORTED(PERIMORTEM) auto parse(const Core::Storage::Bytes<32>& source)
      -> Uuid;
  static EXPORTED(PERIMORTEM) auto parse(const Core::Storage::Bytes<36>& source)
      -> Uuid;

  perimortem_uuid value{};
};

// C and C++ exchange the same two word record by value. Uuid adds methods
// without adding state. Equal size and alignment preserve the object boundary,
// while standard layout and trivial copying avoid C++ lifetime work.
static_assert(sizeof(Uuid) == sizeof(perimortem_uuid));
static_assert(alignof(Uuid) == alignof(perimortem_uuid));
static_assert(__is_standard_layout(Uuid));
static_assert(__is_trivially_copyable(Uuid));

}  // namespace Perimortem::Serialization::Formats
