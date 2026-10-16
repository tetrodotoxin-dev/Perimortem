// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/memory/lifetime.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/memory/allocator/bibliotheca.hpp"

namespace Perimortem::Memory::Dynamic {

// Copies share one value and a reservation on its Bibliotheca allocation. The
// last Record destroys that value before returning the allocation. Records
// stay on the allocating thread because Bibliotheca is thread local.
//
// Move construction retains the source value. Move assignment exchanges the
// two values, so both Records remain valid.
template <typename value_type>
class Record {
 public:
  // Constructs a new Record allocation by copying `source`. Explicit
  // construction keeps the allocation and independent value visible at the
  // call site.
  explicit Record(const value_type& source)
    requires(__is_constructible(value_type, const value_type&))
      : Record([&source] { return value_type(source); }) {}

  // Constructs a new Record allocation by moving from `source`. Explicit
  // construction keeps the allocation and source state change visible at the
  // call site.
  explicit Record(value_type&& source)
    requires(__is_constructible(value_type, value_type &&))
      : Record([&source] { return value_type(Core::Data::take(source)); }) {}

  // The factory constructs the value directly in Bibliotheca storage so its
  // lifetime begins under the shared reservation. Explicit construction
  // prevents an unrelated callable from implicitly creating a Record.
  template <typename factory_type>
  explicit Record(factory_type factory)
    requires requires(factory_type create) {
      requires __is_same(value_type, decltype(create()));
    }
  {
    static_assert(
        alignof(value_type) <= Allocator::Bibliotheca::allocation_alignment);
    auto allocation = Allocator::Bibliotheca::check_out(sizeof(value_type));
    value = &Toolchain::Memory::Lifetime::construct<value_type>(
        allocation.get_data(), Core::Data::take(factory));
  }

  Record(Record& rhs) : value(rhs.value) {
    Allocator::Bibliotheca::reserve(Core::Data::cast<U8>(value));
  }

  Record(const Record& rhs) : value(rhs.value) {
    Allocator::Bibliotheca::reserve(Core::Data::cast<U8>(value));
  }

  Record(Record&& rhs) : Record(rhs) {}

  auto operator=(const Record& rhs) -> Record& {
    if (value == rhs.value) {
      return *this;
    }

    Record replacement(rhs);
    Core::Data::swap(value, replacement.value);
    return *this;
  }

  auto operator=(Record&& rhs) -> Record& {
    if (this == &rhs) {
      return *this;
    }

    Core::Data::swap(value, rhs.value);
    return *this;
  }

  ~Record() {
    auto allocation = Core::Data::cast<U8>(value);
    if (Allocator::Bibliotheca::reservation_count(allocation) == 1) {
      value->~value_type();
    }

    Allocator::Bibliotheca::remit(allocation);
  }

  constexpr auto operator->() -> value_type* { return value; }
  constexpr auto operator->() const -> const value_type* { return value; }
  constexpr auto operator*() -> value_type& { return *value; }
  constexpr auto operator*() const -> const value_type& { return *value; }

  // A retaining cache can retire its entry when only cache reservations remain.
  auto get_reservations() const -> Count {
    return Allocator::Bibliotheca::reservation_count(
        Core::Data::cast<U8>(value));
  }

 private:
  value_type* value;
};

}  // namespace Perimortem::Memory::Dynamic
