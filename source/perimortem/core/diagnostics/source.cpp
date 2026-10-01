// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/diagnostics/source.hpp"

#include "perimortem/core/null_terminated.hpp"

using namespace Perimortem::Core;

// Compiler attribution supplies C strings without lengths. Limit each name
// to 4096 bytes before passing it to diagnostic consumers.
constexpr Count max_source_name_size = 4096;

auto Diagnostics::Source::is_set() const -> Bool {
  return impl != nullptr || !file.is_empty();
}

auto Diagnostics::Source::get_line() const -> Count {
  if (!file.is_empty()) {
    return line;
  }

  if (impl == nullptr) {
    return 0;
  }

  return Count(impl->_M_line);
}

auto Diagnostics::Source::get_column() const -> Count {
  if (!file.is_empty()) {
    return column;
  }

  if (impl == nullptr) {
    return 0;
  }

  return Count(impl->_M_column);
}

auto Diagnostics::Source::get_file() const -> View::Bytes {
  if (!file.is_empty()) {
    return file;
  }

  if (impl == nullptr) {
    return View::Bytes();
  }

  return NullTerminated::convert_cstring(
      impl->_M_file_name, max_source_name_size);
}

auto Diagnostics::Source::get_function() const -> View::Bytes {
  if (!file.is_empty()) {
    return function;
  }

  if (impl == nullptr) {
    return View::Bytes();
  }

  return NullTerminated::convert_cstring(
      impl->_M_function_name, max_source_name_size);
}
