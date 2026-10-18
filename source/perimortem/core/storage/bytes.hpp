// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/access/bytes.h"
#include "perimortem/core/algorithm/hash.hpp"
#include "perimortem/core/data.hpp"
#include "perimortem/core/math/scalar.hpp"
#include "perimortem/core/view/bytes.h"

namespace Perimortem::Core::Storage {

// Bytes stores exactly `literal_size` inline bytes. Its extent never changes,
// so views and writable access remain at the same address for the value's
// lifetime. Copying duplicates the complete array without using an allocator.
//
// Constructors can populate the storage during constant evaluation. A shorter
// View copies its available bytes and zero fills the remainder, while a longer
// View is truncated to the fixed extent.
template <Count literal_size>
class Bytes {
 private:
  struct Values {
    U8 source_block[literal_size];
  };

  Values values{};

 public:
  constexpr Bytes() {}

  // Aggregate construction initializes the complete storage from one source.
  // Call sites use an extra pair of braces, as in
  // `Storage::Bytes<3> bytes = {{1, 2, 3}}`.
  constexpr Bytes(const Values& source) : values(source) {}

  // Copies as many source bytes as fit in the fixed extent and zero fills the
  // remaining bytes.
  constexpr Bytes(const View::Bytes& source) {
    const Count size = Core::Math::Scalar::min(literal_size, source.get_size());
    if consteval {
      Count i = 0;
      for (; i < size; i++) {
        values.source_block[i] = source.get_data()[i];
      }

      for (; i < literal_size; i++) {
        values.source_block[i] = 0;
      }
    } else {
      Data::copy(values.source_block, source.get_data(), size);
      Data::set(values.source_block + size, 0, literal_size - size);
    }
  }

  // Builds byte tables from an index function. `generator` supplies one byte
  // for each index in ascending order and can run during constant evaluation.
  constexpr Bytes(U8 (*generator)(Count)) {
    for (Count i = 0; i < literal_size; i++) {
      values.source_block[i] = generator(i);
    }
  }

  // Copies exactly `literal_size` bytes. The caller supplies a readable range
  // of at least that size.
  constexpr Bytes(const U8* source) {
    if consteval {
      for (Count i = 0; i < literal_size; i++) {
        values.source_block[i] = source[i];
      }
    } else {
      if constexpr (literal_size < 16) {
        for (Count i = 0; i < literal_size; i++) {
          values.source_block[i] = source[i];
        }
      } else {
        Data::copy(values.source_block, source, literal_size);
      }
    }
  }

  constexpr operator View::Bytes() const { return get_view(); }
  constexpr operator Access::Bytes() { return get_access(); }

  constexpr auto operator==(const View::Bytes& rhs) -> Bool {
    if (rhs.get_size() != literal_size) {
      return False;
    }

    return Data::compare(values.source_block, rhs.get_data(), literal_size);
  }

  constexpr auto operator!=(const View::Bytes& rhs) -> Bool {
    return !(*this == rhs);
  }

  constexpr auto operator[](Count index) -> U8& {
    return values.source_block[index];
  }

  constexpr auto operator[](Count index) const -> const U8& {
    return values.source_block[index];
  }

  constexpr auto slice(Count start, Count size) const -> View::Bytes {
    if (start >= get_size()) {
      return View::Bytes();
    }

    return View::Bytes(
        values.source_block + start,
        Core::Math::Scalar::min(size, get_size() - start));
  }

  constexpr auto get_size() const -> Count { return literal_size; }
  constexpr auto get_capacity() const -> Count { return literal_size; }
  constexpr auto get_view() const -> const View::Bytes {
    return View::Bytes(values.source_block, literal_size);
  }

  constexpr auto get_data() const -> const U8* { return values.source_block; }
  constexpr auto get_data() -> U8* { return values.source_block; }
  constexpr auto get_access() -> Access::Bytes {
    return Access::Bytes(values.source_block, literal_size);
  }

  constexpr auto hash() const -> U64 {
    return Core::Algorithm::Hash(get_view()).get_value();
  }
};

}  // namespace Perimortem::Core::Storage
