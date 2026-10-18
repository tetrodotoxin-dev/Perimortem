// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "validation/tests/log.hpp"

#include "perimortem/core/algorithm/search.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/storage/bytes.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Tests;

static Diagnostics::Log::Level captured_level;
static Storage::Bytes<2048> captured;
static Count captured_size;
static Count capture_count;

static auto capture(
    Diagnostics::Log::Level level,
    View::Bytes message,
    const Diagnostics::Source&) -> void {
  captured_level = level;
  captured_size = message.get_size() < captured.get_size()
                      ? message.get_size()
                      : captured.get_size();
  captured = message;
  ++capture_count;
}

auto Log::begin() -> void {
  captured_size = 0;
  capture_count = 0;
  Diagnostics::Log::set_sink(capture);
}

auto Log::end() -> void {
  Diagnostics::Log::set_sink(Diagnostics::Log::default_sink);
}

auto Log::captured_message() -> View::Bytes {
  return captured.slice(0, captured_size);
}

auto Log::captured_count() -> Count {
  return capture_count;
}

auto Log::warning_contains(View::Bytes message) -> bool {
  return captured_level == Diagnostics::Log::Level::Warning &&
         Algorithm::Search::match(captured_message(), message) != Count(-1);
}

auto Log::error_contains(View::Bytes message) -> bool {
  return captured_level == Diagnostics::Log::Level::Error &&
         Algorithm::Search::match(captured_message(), message) != Count(-1);
}
