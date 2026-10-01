// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/diagnostics/log.hpp"

#include "validation/unit_tests/log.hpp"
#include "perimortem/core/algorithm/search.hpp"
#include "perimortem/core/static/bytes.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Tests;

static Diagnostics::Log::Level captured_level;
static Static::Bytes<2048> captured;
static Count captured_size;

static auto capture(Diagnostics::Log::Level level, View::Bytes message,
                    const Diagnostics::Source&) -> void {
  captured_level = level;
  captured_size = message.get_size() < captured.get_size() ? message.get_size() : captured.get_size();
  captured = message;
}

auto Log::begin() -> void {
  captured_size = 0;
  Diagnostics::Log::set_sink(capture);
}

auto Log::end() -> void {
  Diagnostics::Log::set_sink(Diagnostics::Log::default_sink);
}

auto Log::captured_message() -> View::Bytes {
  return captured.slice(0, captured_size);
}

auto Log::error_contains(View::Bytes message) -> bool {
  return captured_level == Diagnostics::Log::Level::Error && Algorithm::search(captured_message(), message) != Count(-1);
}
