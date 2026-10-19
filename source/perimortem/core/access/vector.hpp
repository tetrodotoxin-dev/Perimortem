// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/math/scalar.hpp"
#include "perimortem/core/storage/option.hpp"
#include "perimortem/core/view/vector.hpp"

namespace Perimortem::Core::Access {

// Vector provides allocation free write access to one contiguous range of typed
// values. Indexing and slicing never access values outside the provided range
// with out of bounds indices returning None. `get_data` is the explicit escape
// for code that needs to optimize for speed by providing raw pointer access.
// `get_view` provides read only access to the same range.
//
// Vector does not extend the source storage lifetime. Relocating or releasing
// the referenced storage invalidates every Access and View into it.
//
// An empty Access has zero extent while `get_data` continues to return its
// supplied storage address.
template <typename type>
class Vector {
 public:
  using data_type = type;

  Vector() = delete;
  constexpr Vector(const Vector&) = default;

  // A pointer cannot express the reference required by Access. Creation
  // rejects a null address even when the supplied extent is zero.
  static constexpr auto create(data_type* source, Count source_size)
      -> Core::Storage::Option<Vector> {
    if (source == nullptr) {
      return {};
    }

    return Vector(source, source_size);
  }

  // `source` begins one contiguous range containing `source_size` writable
  // values. The caller keeps that complete range accessible throughout use of
  // the Access.
  constexpr Vector(data_type& source, Count source_size)
      : source_block(&source), size(source_size) {}
  constexpr Vector(data_type&&, Count) = delete;

  template <Count N>
  constexpr Vector(data_type (&source)[N]) : source_block(source), size(N) {}
  template <Count N>
  constexpr Vector(data_type (&&)[N]) = delete;

  constexpr auto operator[](Count index) -> Core::Storage::Option<data_type&> {
    if (is_empty() || index >= get_size()) [[unlikely]] {
      return {};
    }

    return source_block[index];
  }

  constexpr auto slice(Count start, Count size = Count(-1)) const
      -> Access::Vector<data_type> {
    if (start >= get_size()) {
      return Access::Vector<data_type>(source_block, 0);
    }

    return Access::Vector<data_type>(
        source_block + start,
        Core::Math::Scalar::min(size, get_size() - start));
  };

  constexpr auto is_empty() const -> Bool { return size == 0; };
  constexpr auto get_size() const -> Count { return size; };
  constexpr auto get_data() -> data_type* { return source_block; };
  constexpr auto get_view() const -> const View::Vector<data_type> {
    if (is_empty()) {
      return View::Vector<data_type>();
    }

    return View::Vector(source_block[0], get_size());
  }

 private:
  constexpr Vector(data_type* source, Count source_size)
      : source_block(source), size(source_size) {}

  type* source_block;
  Count size;
};

}  // namespace Perimortem::Core::Access
