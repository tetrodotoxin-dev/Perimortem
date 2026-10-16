// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#ifndef PERIMORTEM_CORE_VIEW_BYTES_H
#define PERIMORTEM_CORE_VIEW_BYTES_H

#include "toolchain/toolchain.h"

// The ABI represents read only bytes with one pointer and byte count. An empty
// record carries a null pointer and a zero size. The source storage remains
// available at the same address through every use of the record.
typedef struct perimortem_view_bytes {
  const U8* data;
  Count size;
} perimortem_view_bytes;

#ifdef __cplusplus

#include "toolchain/toolchain.hpp"

#include "perimortem/core/data.hpp"

namespace Perimortem::Core::View {

// A raw read only view of continuous bytes with no endianness.
//
// Byte views are typically used as strings but this is only valid when the
// encoding is ASCII. The main purpose of a byte view is to guarantee the data
// lie on 1 byte boundries (for instance ASCII is 7 bit aligned at the byte).
// Any optimized vectorization opperations will break UTF 8 strings unless you
// know exactly what you are doing (scanning for an exact byte).
//
// Since the structure of the data is undefined, Bytes data does not
// directly convert to Vector data, however Vector data can be converted
// to Bytes data.
//
// View uses perimortem_view_bytes as its ABI type. It carries the same pointer,
// size and storage lifetime as the C record. The C++ layer adds safe empty
// construction and byte operations while storing that record directly.
class Bytes {
 public:
  using data_type = U8;

  constexpr Bytes() = default;

  constexpr Bytes(const Bytes&) = default;

  constexpr Bytes(perimortem_view_bytes value) : value(value) {}

  template <Count extent>
  constexpr Bytes(const U8 (&source)[extent]) : value{source, extent} {}

  constexpr Bytes(const U8* source, Count source_size)
      : value{source, source_size} {}

  Bytes(const char* source, Count source_size)
      : value{Core::Data::cast<const U8>(source), source_size} {}

  constexpr operator perimortem_view_bytes() const { return value; }

  constexpr auto operator==(const Bytes& other) const -> Bool {
    if (value.size != other.value.size) {
      return False;
    }

    for (Count index = 0; index < value.size; index++) {
      if (value.data[index] != other.value.data[index]) {
        return False;
      }
    }

    return True;
  }

  constexpr auto operator!=(const Bytes& other) const -> Bool {
    return !operator==(other);
  }

  constexpr auto operator[](Count index) const -> U8 {
    return index < value.size ? value.data[index] : U8();
  }

  constexpr auto slice(Count start, Count requested_size = Count(-1)) const
      -> Bytes {
    if (start >= value.size) {
      return Bytes();
    }

    const Count available = value.size - start;
    const Count selected_size =
        requested_size < available ? requested_size : available;
    return Bytes(value.data + start, selected_size);
  }

  constexpr auto is_empty() const -> Bool { return value.size == 0; }
  constexpr auto get_size() const -> Count { return value.size; }
  constexpr auto get_data() const -> const U8* { return value.data; }

 private:
  perimortem_view_bytes value{};
};

static_assert(sizeof(Bytes) == sizeof(perimortem_view_bytes));
static_assert(alignof(Bytes) == alignof(perimortem_view_bytes));
static_assert(__is_standard_layout(Bytes));
static_assert(__is_trivially_copyable(Bytes));

}  // namespace Perimortem::Core::View

static_assert(__is_standard_layout(perimortem_view_bytes));
static_assert(__is_trivially_copyable(perimortem_view_bytes));
static_assert(__is_trivial(perimortem_view_bytes));
static_assert(__is_aggregate(perimortem_view_bytes));

#endif

#endif
