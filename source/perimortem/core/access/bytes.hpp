// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/access/bytes.h"
#include "perimortem/core/storage/option.hpp"
#include "perimortem/core/view/bytes.hpp"

namespace Perimortem::Core::Access {

// Bytes provides allocation free write access to one contiguous range of
// arbitrary bytes without assigning them an explicit structure. Indexing and
// slicing never access bytes outside the provided range with out of bounds
// indices returning None. `get_data` is the explicit escape for code that needs
// to optimize for speed by providing raw pointer access. `get_view` provides
// read only access to the same range.
//
// Bytes does not extend the source storage lifetime. Relocating or releasing
// the referenced storage invalidates every Access and View into it.
//
// An empty Access has zero extent while `get_data` continues to return its
// supplied storage address. Bytes stores `perimortem_access_bytes` directly so
// C and C++ exchange the same address and count.
class Bytes {
 public:
  using data_type = U8;

  Bytes() = delete;

  constexpr Bytes(const Bytes&) = default;

  // A C record cannot express the reference required by Access. Creation
  // rejects a null address even when the supplied extent is zero.
  static constexpr auto create(perimortem_access_bytes source)
      -> Core::Storage::Option<Bytes> {
    if (source.data == nullptr) {
      return {};
    }

    return Bytes(source);
  }

  // `source` begins one contiguous range containing `source_size` writable
  // bytes. The caller keeps that complete range accessible throughout use of
  // the Access.
  constexpr Bytes(U8& source, Count source_size)
      : value{&source, source_size} {}
  constexpr Bytes(U8&&, Count) = delete;

  template <Count extent>
  constexpr Bytes(U8 (&source)[extent]) : value{source, extent} {}
  template <Count extent>
  constexpr Bytes(U8 (&&)[extent]) = delete;

  constexpr operator perimortem_access_bytes() const { return value; }
  constexpr operator View::Bytes() const { return get_view(); }

  constexpr auto operator[](Count index) -> Core::Storage::Option<U8&> {
    if (is_empty() || index >= get_size()) [[unlikely]] {
      return {};
    }

    return value.data[index];
  }

  constexpr auto slice(Count start, Count requested_size = Count(-1)) const
      -> Bytes {
    if (start >= value.size) [[unlikely]] {
      return Bytes(value.data[0], 0);
    }

    const Count available = value.size - start;
    const Count selected_size =
        requested_size < available ? requested_size : available;
    return Bytes(value.data[start], selected_size);
  }

  constexpr auto is_empty() const -> Bool { return value.size == 0; }
  constexpr auto get_size() const -> Count { return value.size; }
  constexpr auto get_data() -> U8* { return value.data; }
  constexpr auto get_data() const -> const U8* { return value.data; }
  constexpr auto get_view() const -> View::Bytes {
    if (is_empty()) {
      return View::Bytes();
    }

    return View::Bytes(value.data[0], value.size);
  }

 private:
  constexpr Bytes(perimortem_access_bytes source) : value(source) {}

  perimortem_access_bytes value;
};

// C and C++ exchange the same pointer and size record by value. Bytes adds
// methods without adding state. Equal size and alignment preserve the object
// boundary, while standard layout and trivial copying avoid C++ lifetime work.
static_assert(sizeof(Bytes) == sizeof(perimortem_access_bytes));
static_assert(alignof(Bytes) == alignof(perimortem_access_bytes));
static_assert(__is_standard_layout(Bytes));
static_assert(__is_trivially_copyable(Bytes));

}  // namespace Perimortem::Core::Access
