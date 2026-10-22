// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.hpp"

#include "perimortem/core/identity/uuid.h"

namespace Perimortem::Core::Identity {

// Uuid represents a 128 bit identifier independently of its generation and
// text format. Generation and assignment policies determine what the
// identifier names and any uniqueness guarantees.
//
// All bit patterns are representable, including zero. Equality compares every
// bit. Ordering compares the unsigned high word before the low word. Default
// construction produces zero, and `is_set` tests for a nonzero value.
//
// The complete C record lives inline. Construction, copying and comparison
// support constant evaluation without allocating.
class Uuid {
 public:
  constexpr Uuid() = default;
  constexpr Uuid(const Uuid&) = default;
  constexpr Uuid(perimortem_uuid value) : value(value) {}
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

 private:
  perimortem_uuid value{};
};

// C and C++ exchange the same two word record by value. Uuid adds methods
// without adding state. Equal size and alignment preserve the object boundary,
// while standard layout and trivial copying avoid C++ lifetime work.
static_assert(sizeof(Uuid) == sizeof(perimortem_uuid));
static_assert(alignof(Uuid) == alignof(perimortem_uuid));
static_assert(__is_standard_layout(Uuid));
static_assert(__is_trivially_copyable(Uuid));

}  // namespace Perimortem::Core::Identity
