// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/view/bytes.h"
#include "perimortem/core/view/vector.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/managed/map.hpp"
#include "perimortem/memory/managed/vector.hpp"

namespace Perimortem::System::Process {

// Args safely extracts a raw process argument stream into a Map from each name
// to its authored values. It validates option names and groups repeated options
// and positional values without interpreting their contents. The same
// configuration supplies matching diagnostic help.
//
// Parsing copies every extracted argument into the supplied Arena. The returned
// names, values and grouping containers remain accessible until that Arena
// resets or is destroyed.
//
// Configuration names omit leading dashes. An empty name accepts positional
// values and appears as `<value>` in help. `help` is always recognized and uses
// the standard description.
class Args {
 public:
  // Each accepted name maps to its values in authored order. The application
  // decides which names are required and how many values each accepts.
  using Values = Memory::Managed::
      Map<Core::View::Bytes, Memory::Managed::Vector<Core::View::Bytes>>;

  // Converts byte Views from argc/argv into structured Values. The first View
  // is assumed to be `argv[0]` and is reserved as the process name. Every later
  // View is treated as a positional value or named option. Repeated names group
  // their values in authored order. The caller decides whether duplicates are
  // valid for each name.
  //
  // Every parsed View is copied into the Arena rather than retaining the
  // caller's argument storage. Leading dashes introduce an option, text after
  // the first equals sign supplies its value and an option without an equals
  // sign receives `true`. An unrecognized name logs an error and returns an
  // empty Values map.
  static EXPORTED(PERIMORTEM) auto parse(
      Memory::Allocator::Arena& arena,
      const Memory::Managed::Map<Core::View::Bytes, Core::View::Bytes>& config,
      Core::View::Vector<Core::View::Bytes> arguments) -> Values;

  // Publishes diagnostic help from a configuration in the same form consumed
  // by `parse`. Passing the same configuration keeps accepted names and their
  // descriptions aligned with validation. The command label uses the basename
  // of `argv[0]`, the basename of the host process path when `argv[0]` is
  // unavailable, or the fallback name `process`. Diagnostics publishes the
  // result using the current thread logger at level `Info`.
  static EXPORTED(PERIMORTEM) auto log_help(
      Memory::Allocator::Arena& arena,
      Core::View::Bytes tool_summary,
      const Memory::Managed::Map<Core::View::Bytes, Core::View::Bytes>& config,
      Core::View::Vector<Core::View::Bytes> arguments) -> void;
};

}  // namespace Perimortem::System::Process
