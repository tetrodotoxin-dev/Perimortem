// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/scalar.hpp"
#include "perimortem/core/view/selection.hpp"

namespace Perimortem::Core::View {

// Vector is a borrowed read only range of contiguous values. It preserves the
// value type and native in memory representation without declaring a byte order
// or wire format. The caller keeps every value alive at the same address
// through all access.
template <typename type>
class Vector {
 public:
  using data_type = type;

  constexpr Vector() = default;
  constexpr Vector(const Vector&) = default;
  constexpr Vector(const data_type* entries, const Count size)
      : source_block(entries), size(size) {}

  template <Count N>
  constexpr Vector(const data_type (&source)[N])
      : source_block(source), size(N) {}

  constexpr auto operator==(const Vector& rhs) const -> Bool {
    if (rhs.size != size) {
      return false;
    }

    for (Count i = 0; i < size; i++) {
      if (source_block[i] != rhs.source_block[i]) {
        return false;
      }
    }

    return true;
  }

  constexpr auto operator[](Count index) const -> data_type {
    if (index >= size) [[unlikely]] {
      return data_type();
    }

    return source_block[index];
  }

  constexpr auto slice(Count start, Count size = Count(-1)) const
      -> View::Vector<data_type> {
    if (start >= get_size()) {
      return View::Vector<data_type>();
    }

    return View::Vector<data_type>(
        source_block + start, Core::Scalar::min(size, get_size() - start));
  };

  constexpr auto is_empty() const -> Bool { return size == 0; };
  constexpr auto get_size() const -> Count { return size; }
  constexpr auto get_data() const -> const data_type* { return source_block; }
  constexpr auto begin() const -> Selection<Vector> {
    return Selection<Vector>(*this);
  }

  constexpr auto end() const -> Selection<Vector> {
    return Selection<Vector>(*this, get_size());
  }

 private:
  const type* source_block = nullptr;
  Count size = 0;
};

}  // namespace Perimortem::Core::View
