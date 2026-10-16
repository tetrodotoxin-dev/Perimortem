// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/access/vector.hpp"
#include "perimortem/core/view/vector.hpp"

namespace Perimortem::Core::Storage {

// Vector stores exactly `literal_size` values inline. Its size and capacity
// never change, so views and writable access keep the same address for the
// Vector lifetime. Vector requests no allocation for the array. Construction
// and copying retain each value type's own behavior.
//
// Constructors can populate the complete array during constant evaluation.
// Default construction value initializes every slot. Aggregate construction
// can initialize values without requiring default construction or assignment.
template <typename type, Count literal_size>
class Vector {
 private:
  struct Values {
    type source_block[literal_size];
  };

  Values values{};

 public:
  static constexpr Count size = literal_size;

  constexpr Vector() {}

  // Aggregate construction initializes the complete storage from one source.
  // Call sites use an extra pair of braces to create that source, as in
  // `Storage::Vector<U8, 3> values = {{1, 2, 3}}`.
  //
  // Tables of aggregate elements spell the first value's type so the outer
  // list resolves to Values before C++ elides the remaining element braces.
  //
  // The generated copy constructor for Values constructs every array element
  // in place. This keeps initialization independent of the element count
  // without requiring elements to support default construction or assignment.
  constexpr Vector(const Values& source) : values(source) {}

  // Builds constexpr lookup tables from an index function. Every value is
  // default constructed, then `generator` supplies one assigned value for each
  // index in ascending order. Unlike aggregate construction, this requires
  // `type` to support default construction and assignment.
  constexpr Vector(type (*generator)(Count)) {
    for (Count i = 0; i < literal_size; i++) {
      values.source_block[i] = generator(i);
    }
  }

  constexpr operator Core::View::Vector<type>() const { return get_view(); }
  constexpr operator Core::Access::Vector<type>() { return get_access(); }

  constexpr auto operator==(const View::Vector<type>& rhs) -> Bool {
    if (rhs.get_size() != literal_size) {
      return False;
    }

    return Data::compare(values.source_block, rhs.get_data(), literal_size);
  }

  constexpr auto operator!=(const View::Vector<type>& rhs) -> Bool {
    return !(*this == rhs);
  }

  constexpr auto operator[](Count index) -> type& {
    return values.source_block[index];
  }

  constexpr auto operator[](Count index) const -> const type& {
    return values.source_block[index];
  }

  constexpr auto slice(Count start, Count size = Count(-1)) const
      -> Core::View::Vector<type> {
    return get_view().slice(start, size);
  }

  constexpr auto is_empty() const -> Bool { return literal_size == 0; };
  constexpr auto get_size() const -> Count { return literal_size; }
  constexpr auto get_capacity() const -> Count { return literal_size; }
  constexpr auto get_view() const -> const Core::View::Vector<type> {
    return Core::View::Vector<type>(values.source_block, literal_size);
  }

  constexpr auto get_data() const -> const type* { return values.source_block; }

  constexpr auto get_data() -> type* { return values.source_block; }
  constexpr auto get_access() -> Core::Access::Vector<type> {
    return Core::Access::Vector<type>(values.source_block, literal_size);
  }
};

}  // namespace Perimortem::Core::Storage
