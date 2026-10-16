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

  // Groups authored values by a key declared in `config` while preserving their
  // order. Parsing skips the executable name. The empty key collects positional
  // values and `help` is always recognized. An unrecognized name logs an error
  // and returns an empty Values map.
  static EXPORTED(PERIMORTEM) auto parse(
      Memory::Allocator::Arena& arena,
      const Memory::Managed::Map<Core::View::Bytes, Core::View::Bytes>& config,
      Core::View::Vector<Core::View::Bytes> arguments) -> Values;

  // Formats the same declarations consumed by `parse`. The executable name
  // comes from the invocation when available and otherwise from the current
  // process. Diagnostics publishes the result using the current logger for the
  // thread at log level `Info`.
  static EXPORTED(PERIMORTEM) auto log_help(
      Memory::Allocator::Arena& arena,
      Core::View::Bytes tool_summary,
      const Memory::Managed::Map<Core::View::Bytes, Core::View::Bytes>& config,
      Core::View::Vector<Core::View::Bytes> arguments) -> void;
};

}  // namespace Perimortem::System::Process
