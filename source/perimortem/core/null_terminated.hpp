// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/data.hpp"
#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/core/view/bytes.h"

namespace Perimortem::Core {

// NullTerminated calculates the byte extent of a bounded C string once. The
// resulting View uses the source storage and excludes the first null byte. It
// remains valid while the caller keeps that storage alive at the same address.
class NullTerminated {
 public:
  constexpr NullTerminated(const char* source, Count capacity)
      : source(Data::cast<const U8>(source)) {
    if (source) {
      while (size < capacity && source[size] != 0) {
        size++;
      }
    }
  }

  auto get_view() const -> View::Bytes { return View::Bytes(source, size); }

  // Literal stores every authored byte except C++'s implicit terminator.
  // Authors include an explicit null when it belongs to the resulting byte
  // view.
  template <CppSize size_including_null>
  struct Literal {
    U8 content[size_including_null - 1]{};

    consteval Literal(const char (&source)[size_including_null]) {
      for (Count i = 0; i < get_size(); i++) {
        content[i] = U8(source[i]);
      }
    }

    consteval operator View::Bytes() const { return get_view(); }

    consteval auto get_size() const -> Count { return size_including_null - 1; }
    consteval auto get_data() const -> const U8* { return content; }
    consteval auto get_view() const -> const View::Bytes {
      return View::Bytes(content, get_size());
    }
  };

 private:
  const U8* source = nullptr;
  Count size = 0;
};

}  // namespace Perimortem::Core

// Produces a compile time View containing every authored literal byte except
// C++'s implicit final terminator. Authored null bytes remain in the View.
template <Perimortem::Core::NullTerminated::Literal c_string>
consteval const Perimortem::Core::View::Bytes operator""_view() {
  return c_string.get_view();
}

template <Perimortem::Core::NullTerminated::Literal c_string>
consteval const Perimortem::Core::Storage::Bytes<c_string.get_size()>
    operator""_bytes() {
  return Perimortem::Core::Storage::Bytes<c_string.get_size()>(
      c_string.get_view());
}
