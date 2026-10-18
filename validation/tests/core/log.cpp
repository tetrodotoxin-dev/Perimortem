// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/diagnostics/log.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/algorithm/search.hpp"
#include "perimortem/core/data.hpp"
#include "perimortem/core/math/scalar.hpp"
#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/core/storage/vector.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/core/writer/textual.hpp"

using namespace Perimortem::Core;
using namespace Toolchain::Validation;

constexpr Count max_message_length = 256;

struct LogEvent {
  Storage::Bytes<max_message_length> message;
  Count message_size;
};

constexpr Count event_log_size = 8;
static Storage::Vector<LogEvent, event_log_size> log_events;
static Count total_events = 0;

static auto capture_sink(
    Diagnostics::Log::Level level,
    View::Bytes message,
    const Diagnostics::Source& location) -> void {
  Storage::Bytes<max_message_length> formatted;
  Count formatted_size = Diagnostics::Log::format_entry(
      level, message, location, formatted.get_access());
  Count index = total_events++ % event_log_size;
  log_events[index].message_size =
      Math::Scalar::min(max_message_length, formatted_size);
  log_events[index].message = formatted;
}

static auto last_entry() -> View::Bytes {
  auto& event = log_events[(total_events - 1) % event_log_size];
  return View::Bytes(event.message.get_data(), event.message_size);
}

static auto contains(View::Bytes haystack, View::Bytes message) -> Bool {
  return Algorithm::Search::match(haystack, message) != Count(-1);
}

static auto has_valid_header(View::Bytes entry) -> Bool {
  if (entry.get_size() < 6) {
    return false;
  }

  switch (entry[0]) {
  case 'D':
  case 'I':
  case 'W':
  case 'E':
  case 'F':
    break;
  default:
    return false;
  }

  Count cursor = 2;
  Count minute_digits = 0;
  while (cursor < entry.get_size() && entry[cursor] >= '0' &&
         entry[cursor] <= '9') {
    minute_digits++;
    cursor++;
  }

  if (minute_digits < 2 || cursor + 10 >= entry.get_size()) {
    return false;
  }

  const U8* elapsed = entry.get_data() + cursor;
  if (elapsed[0] != ':' || elapsed[3] != ':' || elapsed[7] != '.' ||
      elapsed[9] != ' ' || elapsed[10] != '[') {
    return false;
  }

  constexpr Count digit_offsets[] = {1, 2, 4, 5, 6, 8};
  for (Count offset : digit_offsets) {
    if (elapsed[offset] < '0' || elapsed[offset] > '9') {
      return false;
    }
  }

  return true;
}

static Harness DiagnosticsLog = {
  .name = "Diagnostics::Log",
  .setup =
      []() {
        Diagnostics::Log::set_sink(capture_sink);
        Diagnostics::Log::set_level(Diagnostics::Log::Level::Debug);
        Diagnostics::Log::set_disable_header(False);
        total_events = 0;
      },

  .teardown =
      []() {
        Diagnostics::Log::set_sink(Diagnostics::Log::default_sink);
        Diagnostics::Log::set_level(Diagnostics::Log::Level::Info);
        Diagnostics::Log::set_disable_header(False);
        total_events = 0;
      },
};

VALIDATION_TEST(DiagnosticsLog, info_record) {
  Diagnostics::Log::info("unique message string"_view);

  View::Bytes entry = last_entry();

  ASSERT(entry.get_size() > 0);
  EXPECT(has_valid_header(entry));
  EXPECT_EQ(entry[entry.get_size() - 1], U8('\n'));
  EXPECT(contains(entry, "validation/tests/core/log.cpp:"_view));
  EXPECT(contains(entry, "[main]"_view));
  EXPECT(contains(entry, "unique message string"_view));
}

VALIDATION_TEST(DiagnosticsLog, authored_source) {
  constexpr Diagnostics::Source source(
      "authored.cpp"_view, Count(42), Count(7), "operation"_view);

  EXPECT(source.is_set());
  EXPECT_EQ(source.get_file(), "authored.cpp"_view);
  EXPECT_EQ(source.get_function(), "operation"_view);
  EXPECT_EQ(source.get_line(), Count(42));
  EXPECT_EQ(source.get_column(), Count(7));
}

VALIDATION_TEST(DiagnosticsLog, source_less_entry) {
  Diagnostics::Log::log(
      Diagnostics::Log::Level::Info, "source less diagnostic"_view,
      Diagnostics::Source());

  const View::Bytes entry = last_entry();
  EXPECT(contains(entry, "[main] source less diagnostic"_view));
  EXPECT_NOT(contains(entry, ":0:0:"_view));
  EXPECT(has_valid_header(entry));
}

VALIDATION_TEST(DiagnosticsLog, message_raii_guard) {
  Count events_before = total_events;

  {
    Diagnostics::Log::Message<64> message(Diagnostics::Log::Level::Info);
    message << "builder emitted value="_view << U32(42);
    EXPECT_EQ(total_events, events_before);
  }

  EXPECT(total_events > events_before);
  EXPECT(contains(last_entry(), "builder emitted value=42"_view));
  EXPECT(has_valid_header(last_entry()));
}

VALIDATION_TEST(DiagnosticsLog, suppress_messages) {
  Diagnostics::Log::set_level(Diagnostics::Log::Level::Error);

  Count events_before = total_events;

  Diagnostics::Log::error("should pass through"_view);
  EXPECT(total_events > events_before);
  events_before = total_events;

  Diagnostics::Log::info("should be suppressed"_view);
  EXPECT_EQ(total_events, events_before);

  // Filtering the Info record prevents the sink from observing it, leaving the
  // preceding Error as the last captured record.
  EXPECT(contains(last_entry(), "should pass through"_view));
  EXPECT(has_valid_header(last_entry()));
}

static auto logging_function() -> void {
  Diagnostics::Log::error("Test Attribution"_view);
}

static auto attributing_function() -> void {
  auto scope_attribution = Diagnostics::Log::set_attribution();
  logging_function();
}

VALIDATION_TEST(DiagnosticsLog, attribution) {
  attributing_function();

  auto logged = last_entry();
  EXPECT(contains(logged, "[main] validation/tests/core/log.cpp:"_view));
  EXPECT(contains(logged, "Test Attribution"_view));
  EXPECT(has_valid_header(last_entry()));
}
