// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/bibliotheca.hpp"
#include "perimortem/core/data.hpp"

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
  template <typename... arg_types>
  Record(arg_types&&... args) {
    static_assert(
        alignof(value_type) <= Core::Bibliotheca::allocation_alignment);
    auto allocation = Core::Bibliotheca::check_out(sizeof(value_type));
    value = new (allocation.ptr, Core::Placement::Construct)
        value_type(static_cast<arg_types&&>(args)...);
  }

  Record(Record& rhs) : value(rhs.value) {
    Core::Bibliotheca::reserve(Core::Data::cast<U8>(value));
  }
  Record(const Record& rhs) : value(rhs.value) {
    Core::Bibliotheca::reserve(Core::Data::cast<U8>(value));
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
    if (Core::Bibliotheca::reservation_count(allocation) == 1) {
      value->~value_type();
    }

    Core::Bibliotheca::remit(allocation);
  }

  constexpr auto operator->() -> value_type* { return value; }
  constexpr auto operator->() const -> const value_type* { return value; }
  constexpr auto operator*() -> value_type& { return *value; }
  constexpr auto operator*() const -> const value_type& { return *value; }

  // A retaining cache can retire its entry when only cache reservations remain.
  auto get_reservations() const -> Count {
    return Core::Bibliotheca::reservation_count(Core::Data::cast<U8>(value));
  }

 private:
  value_type* value;
};

}  // namespace Perimortem::Memory::Dynamic
