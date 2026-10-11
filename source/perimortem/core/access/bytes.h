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

#include "perimortem/core/option.hpp"
#include "perimortem/core/view/bytes.h"

namespace Perimortem::Core::Access {

// Bytes provides read and write Access to byte storage with no endianness.
// Indexed access selects the nearest endpoint when the index lies outside
// nonempty storage. Empty storage has no endpoint, so indexed access returns
// None. get_data exposes raw storage for callers that can prove their bounds.
//
// Access uses perimortem_access_bytes as its ABI type. It carries the same
// pointer, size and storage lifetime as the C record. The C++ layer adds safe
// empty construction, saturated indexing and View conversion while storing
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

  constexpr auto at(Count index) -> Core::Option<U8&> {
    if (is_empty()) [[unlikely]] {
      return {};
    }

    return value.data[index < value.size ? index : value.size - 1];
  }

  template <typename index_type>
    requires(
        __is_integral(index_type) && __is_signed(index_type) &&
        sizeof(index_type) <= sizeof(S64))
  constexpr auto at(index_type index) -> Core::Option<U8&> {
    if (is_empty()) [[unlikely]] {
      return {};
    }

    if (index < 0) {
      return value.data[0];
    }

    return at(Count(index));
  }

  constexpr auto operator[](Count index) -> Core::Option<U8&> {
    return at(index);
  }

  template <typename index_type>
    requires(
        __is_integral(index_type) && __is_signed(index_type) &&
        sizeof(index_type) <= sizeof(S64))
  constexpr auto operator[](index_type index) -> Core::Option<U8&> {
    return at(index);
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
  constexpr auto get_data() -> U8* { return value.data; }
  constexpr auto get_data() const -> const U8* { return value.data; }
  constexpr auto get_view() const -> View::Bytes {
    return View::Bytes(value.data, value.size);
  }

 private:
  perimortem_access_bytes value{};
};

static_assert(sizeof(Bytes) == sizeof(perimortem_access_bytes));
static_assert(alignof(Bytes) == alignof(perimortem_access_bytes));
static_assert(__is_standard_layout(Bytes));
static_assert(__is_trivially_copyable(Bytes));

}  // namespace Perimortem::Core::Access

static_assert(__is_standard_layout(perimortem_access_bytes));
static_assert(__is_trivially_copyable(perimortem_access_bytes));
static_assert(__is_trivial(perimortem_access_bytes));
static_assert(__is_aggregate(perimortem_access_bytes));

#endif

#endif
