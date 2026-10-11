// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/view/bytes.h"
#include "perimortem/core/view/vector.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/managed/map.hpp"
#include "perimortem/memory/managed/vector.hpp"

namespace Perimortem::System::Process {

// Args parses one process invocation into named, positional and repeated
// values retained by the caller's Arena.
//
// The configuration declares the accepted names and their help text. An empty
// name accepts positional values, while `help` is always available. Parsing
// establishes membership and leaves interpretation of each value with the
// application that declared its argument.
class Args {
 public:
  // Each accepted name maps to its values in authored order. The application
  // decides which names are required and how many values each accepts.
  using Values = Memory::Managed::
      Map<Core::View::Bytes, Memory::Managed::Vector<Core::View::Bytes>*>;

  // Takes a map of named variable keys and help text as values.
  // A key of "" (empty view) is used if the tool wants to support positionals.
  static EXPORTED(PERIMORTEM) auto parse(
      Memory::Allocator::Arena& arena,
      const Memory::Managed::Map<Core::View::Bytes, Core::View::Bytes>& config,
      Core::View::Vector<Core::View::Bytes> arguments) -> Values;

  // Writes help for the same configuration through Diagnostics at Info.
  static EXPORTED(PERIMORTEM) auto log_help(
      Memory::Allocator::Arena& arena,
      Core::View::Bytes tool_summary,
      const Memory::Managed::Map<Core::View::Bytes, Core::View::Bytes>& config,
      Core::View::Vector<Core::View::Bytes> arguments) -> void;
};

}  // namespace Perimortem::System::Process
