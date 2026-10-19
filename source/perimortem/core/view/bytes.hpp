// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/view/bytes.h"

namespace Perimortem::Core::View {

// Bytes provides allocation free read access to one contiguous range of
// arbitrary bytes without assigning them an explicit structure. Indexing and
// slicing never read outside the provided range with out of bounds indices
// returning zeros. `get_data` is the explicit escape for code that needs to
// optimize for speed by providing raw pointer access.
//
// Bytes does not extend the source storage lifetime. Relocating or releasing
// the referenced storage invalidates every View into it.
//
// An empty View is zero initialized and returns nullptr for `get_data()`.
class Bytes {
 public:
  using data_type = U8;

  constexpr Bytes() = default;

  constexpr Bytes(const Bytes&) = default;

  // A C record cannot express the reference constraint. A null address or zero
  // count therefore produces the canonical empty View.
  constexpr Bytes(perimortem_view_bytes source)
      : value{
          source.size == 0 ? nullptr : source.data,
          source.data ? source.size : 0} {}

  template <Count extent>
  constexpr Bytes(const U8 (&source)[extent]) : value{source, extent} {}
  template <Count extent>
  constexpr Bytes(const U8 (&&)[extent]) = delete;

  // `source` begins one contiguous range containing `source_size` bytes. The
  // caller keeps that complete range accessible throughout use of the View.
  constexpr Bytes(const U8& source, Count source_size)
      : value{source_size == 0 ? nullptr : &source, source_size} {}
  constexpr Bytes(const U8&&, Count) = delete;

  Bytes(const char& source, Count source_size)
      : value{
          source_size == 0 ? nullptr : Core::Data::cast<const U8>(&source),
          source_size} {}

  Bytes(const char&&, Count) = delete;

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
    if (selected_size == 0) {
      return Bytes();
    }

    return Bytes(value.data[start], selected_size);
  }

  constexpr auto is_empty() const -> Bool { return value.size == 0; }
  constexpr auto get_size() const -> Count { return value.size; }
  constexpr auto get_data() const -> const U8* { return value.data; }

 private:
  perimortem_view_bytes value{};
};

// C and C++ exchange the same pointer and size record by value. Bytes adds
// methods without adding state. Equal size and alignment preserve the object
// boundary, while standard layout and trivial copying avoid C++ lifetime work.
static_assert(sizeof(Bytes) == sizeof(perimortem_view_bytes));
static_assert(alignof(Bytes) == alignof(perimortem_view_bytes));
static_assert(__is_standard_layout(Bytes));
static_assert(__is_trivially_copyable(Bytes));

}  // namespace Perimortem::Core::View
