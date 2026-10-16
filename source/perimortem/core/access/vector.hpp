// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/scalar.hpp"
#include "perimortem/core/storage/option.hpp"
#include "perimortem/core/view/vector.hpp"

namespace Perimortem::Core::Access {

// Vector borrows caller supplied contiguous values for typed mutation and
// slicing without allocating or changing their lifetimes. Indexing returns
// None outside the published size. `get_data()` exposes the raw address for
// callers that prove their own bounds. Binary serialization remains explicit
// because the values retain their native in memory representation.
template <typename type>
class Vector {
 public:
  using data_type = type;

  constexpr Vector() = default;
  constexpr Vector(const Vector&) = default;

  constexpr Vector(data_type* source, Count source_size)
      : source_block(source), size(source ? source_size : 0) {}

  template <Count N>
  constexpr Vector(data_type (&source)[N]) : source_block(source), size(N) {}

  constexpr auto operator[](Count index) -> Core::Storage::Option<data_type&> {
    if (is_empty() || index >= get_size()) [[unlikely]] {
      return {};
    }

    return source_block[index];
  }

  constexpr auto slice(Count start, Count size = Count(-1)) const
      -> Access::Vector<data_type> {
    if (start >= get_size()) {
      return Access::Vector<data_type>();
    }

    return Access::Vector<data_type>(
        source_block + start, Core::Scalar::min(size, get_size() - start));
  };

  constexpr auto is_empty() const -> Bool { return size == 0; };
  constexpr auto get_size() const -> Count { return size; };
  constexpr auto get_data() -> data_type* { return source_block; };
  constexpr auto get_view() const -> const View::Vector<data_type> {
    return View::Vector(source_block, get_size());
  }

 private:
  type* source_block = nullptr;
  Count size = 0;
};

}  // namespace Perimortem::Core::Access
