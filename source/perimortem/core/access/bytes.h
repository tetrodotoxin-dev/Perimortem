// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#ifndef PERIMORTEM_CORE_ACCESS_BYTES_H
#define PERIMORTEM_CORE_ACCESS_BYTES_H

#include "toolchain/toolchain.h"

// The ABI represents writable bytes with one pointer and byte count. An empty
// record carries a null pointer and a zero size. The source storage remains
// available at the same address through every use of the record.
typedef struct perimortem_access_bytes {
  U8* data;
  Count size;
} perimortem_access_bytes;

#ifdef __cplusplus

#include "perimortem/core/storage/option.hpp"
#include "perimortem/core/view/bytes.h"

namespace Perimortem::Core::Access {

// Bytes borrows caller supplied byte storage for mutation and slicing without
// allocating or changing its lifetime. Indexing returns None outside the
// published size. `get_data()` exposes the raw address for callers that prove
// their own bounds.
//
// Access uses perimortem_access_bytes as its ABI type. It carries the same
// pointer, size and storage lifetime as the C record. The C++ layer adds safe
// empty construction, bounds checked indexing and View conversion while storing
// that record directly.
class Bytes {
 public:
  using data_type = U8;

  constexpr Bytes() = default;

  constexpr Bytes(const Bytes&) = default;

  constexpr Bytes(perimortem_access_bytes value)
      : value{value.data, value.data ? value.size : 0} {}

  constexpr Bytes(U8* source, Count source_size)
      : value{source, source ? source_size : 0} {}

  template <Count extent>
  constexpr Bytes(U8 (&source)[extent]) : value{source, extent} {}

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
      return Bytes();
    }

    const Count available = value.size - start;
    const Count selected_size =
        requested_size < available ? requested_size : available;
    return Bytes(value.data + start, selected_size);
  }

  constexpr auto is_empty() const -> Bool { return value.size == 0; }
  constexpr auto get_size() const -> Count { return value.size; }
  constexpr auto get_data() -> U8* { return value.data; }
  constexpr auto get_data() const -> const U8* { return value.data; }
  constexpr auto get_view() const -> View::Bytes {
    return View::Bytes(value.data, value.size);
  }

 private:
  perimortem_access_bytes value{};
};

// C and C++ exchange the same pointer and size record by value. Bytes adds
// methods without adding state. Equal size and alignment preserve the object
// boundary, while standard layout and trivial copying avoid C++ lifetime work.
static_assert(sizeof(Bytes) == sizeof(perimortem_access_bytes));
static_assert(alignof(Bytes) == alignof(perimortem_access_bytes));
static_assert(__is_standard_layout(Bytes));
static_assert(__is_trivially_copyable(Bytes));

}  // namespace Perimortem::Core::Access

#endif

#endif
