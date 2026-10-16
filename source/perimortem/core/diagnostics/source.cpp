// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/diagnostics/source.hpp"

#include "perimortem/core/null_terminated.hpp"

using namespace Perimortem::Core;

// Compiler attribution supplies C strings without lengths. Limit each name
// to 4096 bytes before passing it to diagnostic consumers.
constexpr Count max_source_name_size = 4096;

auto Diagnostics::Source::is_set() const -> Bool {
  return location.is_set() || !file.is_empty();
}

auto Diagnostics::Source::get_line() const -> Count {
  return file.is_empty() ? Count(location.get_line()) : line;
}

auto Diagnostics::Source::get_column() const -> Count {
  return file.is_empty() ? Count(location.get_column()) : column;
}

auto Diagnostics::Source::get_file() const -> View::Bytes {
  if (!file.is_empty()) {
    return file;
  }

  return NullTerminated(location.get_file(), max_source_name_size).get_view();
}

auto Diagnostics::Source::get_function() const -> View::Bytes {
  if (!file.is_empty()) {
    return function;
  }

  return NullTerminated(location.get_function(), max_source_name_size)
      .get_view();
}
