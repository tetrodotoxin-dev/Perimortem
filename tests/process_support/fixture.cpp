// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/perimortem.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef PERI_WINDOWS
#include <windows.h>
#else
#include <sys/resource.h>
#endif

#include "perimortem/core/bibliotheca.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/null_terminated.hpp"

#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/dynamic/vector.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;

// Crash cases live in an executable rather than depending on fork to clone the
// test runner. Returning zero means the allocator failed to reject the request.
static auto run_child(int argc, const char* argv[]) -> int {
  Diagnostics::Log::set_sink(Diagnostics::Log::stderr_sink);
  if (argc < 2) {
    return 3;
  }
  const auto mode = NullTerminated::to_view(argv[1]);
  if (mode == "allocator-limit"_view) {
    Bibliotheca::check_out((Count(1) << 35) + 1);
  } else if (mode == "allocator-overflow"_view) {
    Bibliotheca::check_out(Count(-1));
  } else if (mode == "vector-resize"_view) {
    Dynamic::Vector<U64> values;
    values.resize(Count(1) << 61);
    _Exit(0);
  } else if (mode == "vector-forgetful"_view) {
    Dynamic::Vector<U64> values;
    values.forgetful_resize(Count(1) << 61);
    _Exit(0);
  } else if (mode == "arena-bytes"_view) {
    Allocator::Arena arena;
    arena.allocate(Count(-1));
  } else if (mode == "arena-elements"_view) {
    Allocator::Arena arena;
    arena.reserve<U64>(Count(1) << 61);
  } else {
    return 3;
  }
  return 0;
}

int main(int argc, const char* argv[]) {
#ifdef PERI_WINDOWS
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#else
  const rlimit limit = {0, 0};
  setrlimit(RLIMIT_CORE, &limit);
#endif
  return run_child(argc, argv);
}
