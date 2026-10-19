// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/data.hpp"
#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/core/view/bytes.hpp"

namespace Perimortem::Core::View {

// NullTerminated calculates the byte extent of a bounded C string once. The
// resulting View references the source storage and excludes the first null byte
// without extending that storage's lifetime. Relocating or releasing the source
// invalidates the View, so callers reacquire it after such an operation.
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

  auto get_view() const -> View::Bytes {
    if (size == 0) {
      return View::Bytes();
    }

    return View::Bytes(source[0], size);
  }

  // Literal makes authored bytes a structural template argument for `_view`
  // and `_bytes`. The `_view` template parameter object has static storage
  // duration, so its returned View remains accessible for the program.
  // `_bytes` instead copies those values into its fixed storage. Both forms
  // preserve authored null bytes and exclude only C++'s implicit final
  // terminator.
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
      if constexpr (size_including_null == 1) {
        return View::Bytes();
      }

      return View::Bytes(content[0], get_size());
    }
  };

 private:
  const U8* source = nullptr;
  Count size = 0;
};

}  // namespace Perimortem::Core::View

// Produces a compile time View containing every authored literal byte except
// C++'s implicit final terminator. Authored null bytes remain in the View.
template <Perimortem::Core::View::NullTerminated::Literal c_string>
consteval const Perimortem::Core::View::Bytes operator""_view() {
  return c_string.get_view();
}

template <Perimortem::Core::View::NullTerminated::Literal c_string>
consteval const Perimortem::Core::Storage::Bytes<c_string.get_size()>
    operator""_bytes() {
  return Perimortem::Core::Storage::Bytes<c_string.get_size()>(
      c_string.get_view());
}
