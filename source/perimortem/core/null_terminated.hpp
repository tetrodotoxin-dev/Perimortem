// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/static/bytes.hpp"
#include "perimortem/core/view/bytes.h"

namespace Perimortem::Core {

// A bounded C string calculates its borrowed byte extent once. The resulting
// view excludes the first null byte while retaining the source lifetime.
class NullTerminated {
 public:
  constexpr NullTerminated(const char* source, Count capacity)
      : source(source) {
    if (source) {
      while (size < capacity && source[size] != 0) {
        size++;
      }
    }
  }

  auto get_view() const -> View::Bytes { return View::Bytes(source, size); }

  // Literal owns every authored byte except C++'s implicit terminator. Authors
  // include an explicit null when it belongs to the resulting byte view.
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
  const char* source = nullptr;
  Count size = 0;
};

}  // namespace Perimortem::Core

// Converts C++ string literals into valid Perimortem byte strings.
template <Perimortem::Core::NullTerminated::Literal c_string>
consteval const Perimortem::Core::View::Bytes operator""_view() {
  return c_string.get_view();
}

template <Perimortem::Core::NullTerminated::Literal c_string>
consteval const Perimortem::Core::Static::Bytes<c_string.get_size()>
    operator""_bytes() {
  return Perimortem::Core::Static::Bytes<c_string.get_size()>(
      c_string.get_view());
}
