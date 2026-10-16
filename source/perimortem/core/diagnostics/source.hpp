// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/compile/source_location.hpp"

#include "perimortem/core/view/bytes.h"

namespace Perimortem::Core::Diagnostics {

// Source converts compiler locations into bounded byte views before publishing
// them to Perimortem diagnostics. Explicit byte views let another runtime
// provide the same information without adopting the compiler ABI.
//
// The compiler location points to read only program storage. Explicit views
// retain their caller supplied storage and must remain available through every
// use of Source.
//
// Compiler strings are bounded and converted only when an accessor is called.
// Logging therefore pays that scan only on its diagnostics path.
class Source {
 public:
  // Captures the call site through Toolchain's compiler location utility while
  // retaining Perimortem's safe byte view boundary.
  static consteval auto current(
      Toolchain::Compile::SourceLocation location =
          Toolchain::Compile::SourceLocation::current()) -> Source {
    return Source(location);
  }

  constexpr Source() = default;
  constexpr Source(Toolchain::Compile::SourceLocation location)
      : location(location) {}
  constexpr Source(
      Core::View::Bytes file,
      Count line,
      Count column,
      Core::View::Bytes function = Core::View::Bytes())
      : file(file), function(function), line(line), column(column) {}

  EXPORTED(PERIMORTEM) auto is_set() const -> Bool;
  EXPORTED(PERIMORTEM) auto get_line() const -> Count;
  EXPORTED(PERIMORTEM) auto get_column() const -> Count;
  EXPORTED(PERIMORTEM) auto get_file() const -> Core::View::Bytes;
  EXPORTED(PERIMORTEM) auto get_function() const -> Core::View::Bytes;

 private:
  Toolchain::Compile::SourceLocation location;
  Core::View::Bytes file{};
  Core::View::Bytes function{};
  Count line = 0;
  Count column = 0;
};

}  // namespace Perimortem::Core::Diagnostics
