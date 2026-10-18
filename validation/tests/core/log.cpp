// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/diagnostics/log.hpp"

#include "perimortem/core/diagnostics/message.hpp"

#ifdef TOOLCHAIN_LINUX
#include <glob.h>
#include <stdio.h>
#endif

#include "toolchain/validation/test.hpp"

#include "perimortem/core/algorithm/search.hpp"
#include "perimortem/core/data.hpp"
#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/core/storage/vector.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/core/writer/textual.hpp"
#include "perimortem/system/process/thread.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::System;
using namespace Toolchain::Validation;

constexpr Count max_message_length = 512;

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
  Storage::Bytes<Diagnostics::Log::header_capacity> header_storage;
  const View::Bytes header =
      Diagnostics::Log::generate_header(level, location, header_storage);
  Storage::Bytes<max_message_length> formatted;
  Writer::Textual writer(formatted.get_access());
  writer << header << message;
  if (message[message.get_size() - 1] != '\n') {
    writer << '\n';
  }

  Count index = total_events++ % event_log_size;
  log_events[index].message_size = writer.get_location();
  log_events[index].message = formatted;
}

static auto last_entry() -> View::Bytes {
  auto& event = log_events[(total_events - 1) % event_log_size];
  return View::Bytes(event.message.get_data(), event.message_size);
}

static auto prior_entry() -> View::Bytes {
  auto& event = log_events[(total_events - 2) % event_log_size];
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
        Diagnostics::Log::set_thread_name("main"_view);
        total_events = 0;
      },

  .teardown =
      []() {
        Diagnostics::Log::set_sink(Diagnostics::Log::default_sink);
        Diagnostics::Log::set_level(Diagnostics::Log::Level::Info);
        Diagnostics::Log::set_thread_name("main"_view);
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

VALIDATION_TEST(DiagnosticsLog, display_name) {
  Diagnostics::Log::set_thread_name("worker/name"_view);
  Diagnostics::Log::info("renamed thread"_view);

  const View::Bytes entry = last_entry();
  EXPECT(contains(entry, "[worker/name]"_view));
  EXPECT(contains(entry, "renamed thread"_view));
  EXPECT(has_valid_header(entry));
}

VALIDATION_TEST(DiagnosticsLog, empty_message) {
  Diagnostics::Log::info(View::Bytes());

  EXPECT(contains(last_entry(), "<EMPTY MESSAGE>"_view));
  EXPECT(has_valid_header(last_entry()));
}

VALIDATION_TEST(DiagnosticsLog, message_raii_guard) {
  Count events_before = total_events;

  {
    Diagnostics::Message<64> message(Diagnostics::Log::Level::Info);
    message << "builder emitted value="_view << U32(42);
    EXPECT_EQ(total_events, events_before);
  }

  EXPECT(total_events > events_before);
  EXPECT(contains(last_entry(), "builder emitted value=42"_view));
  EXPECT(has_valid_header(last_entry()));
}

VALIDATION_TEST(DiagnosticsLog, message_capacity_reports_clipping) {
  const Count events_before = total_events;

  {
    Diagnostics::Message<8> message(
        Diagnostics::Log::Level::Info, Diagnostics::Source());
    message << "clipped diagnostic"_view;
  }

  EXPECT_EQ(total_events, events_before + 2);
  EXPECT_EQ(prior_entry()[0], U8('E'));
  EXPECT(contains(prior_entry(), "clipped a diagnostic"_view));
  EXPECT(contains(last_entry(), "clipped "_view));
}

VALIDATION_TEST(DiagnosticsLog, suppressed_message_reports_clipping) {
  Diagnostics::Log::set_level(Diagnostics::Log::Level::Error);

  const Count events_before = total_events;

  {
    Diagnostics::Message<4> message(
        Diagnostics::Log::Level::Info, Diagnostics::Source());
    message << "suppressed diagnostic"_view;
  }

  EXPECT_EQ(total_events, events_before + 1);
  EXPECT_EQ(last_entry()[0], U8('E'));
  EXPECT(contains(last_entry(), "clipped a diagnostic"_view));
}

VALIDATION_TEST(DiagnosticsLog, bounded_header) {
  Storage::Bytes<160> path([](Count) { return U8('p'); });
  Storage::Bytes<Diagnostics::Log::header_capacity> output;
  const Diagnostics::Source source(
      path.get_view(), Count(-1), Count(-1), View::Bytes());

  const View::Bytes header = Diagnostics::Log::generate_header(
      Diagnostics::Log::Level::Info, source, output);

  EXPECT(header.get_size() <= Diagnostics::Log::header_capacity);
  EXPECT(contains(header, "...pppppppp"_view));
  EXPECT(contains(header, "18446744073709551615:18446744073709551615"_view));
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

#ifdef TOOLCHAIN_LINUX

static auto remove_log_files(const char* pattern) -> Count {
  glob_t matches;
  const S32 status = glob(pattern, 0, nullptr, &matches);
  if (status == GLOB_NOMATCH) {
    return 0;
  }

  if (status != 0) {
    return Count(-1);
  }

  const Count count = matches.gl_pathc;
  for (Count index = 0; index < count; index++) {
    remove(matches.gl_pathv[index]);
  }

  globfree(&matches);
  return count;
}

static auto read_and_remove_log_file(const char* pattern, Access::Bytes output)
    -> Count {
  glob_t matches;
  const S32 status = glob(pattern, 0, nullptr, &matches);
  if (status != 0 || matches.gl_pathc != 1) {
    if (status == 0) {
      globfree(&matches);
    }

    return 0;
  }

  FILE* file = fopen(matches.gl_pathv[0], "rb");
  Count size = 0;
  if (file) {
    size = fread(output.get_data(), 1, output.get_size(), file);
    fclose(file);
  }

  remove(matches.gl_pathv[0]);
  globfree(&matches);
  return size;
}

static auto write_and_rename_file_log(void*) -> void {
  Diagnostics::Log::set_sink(Diagnostics::Log::default_sink);
  Diagnostics::Log::info("first file entry"_view);
  Diagnostics::Log::set_thread_name("renamed"_view);
  Diagnostics::Log::info("second file entry"_view);
}

static auto write_large_file_log(void* context) -> void {
  auto& message = *Data::cast<Storage::Bytes<4096>>(context);
  Diagnostics::Log::set_sink(Diagnostics::Log::default_sink);
  Diagnostics::Log::info(message.get_view(), Diagnostics::Source());
  Diagnostics::Log::set_sink(nullptr);
}

VALIDATION_TEST(DiagnosticsLog, stable_sanitized_file_identity) {
  constexpr auto initial_pattern =
      "perimortem_diagnostics.validation_name_x_*.log";
  constexpr auto renamed_pattern = "perimortem_renamed_*.log";
  EXPECT_EQ(remove_log_files(initial_pattern), Count(0));
  EXPECT_EQ(remove_log_files(renamed_pattern), Count(0));

  auto started = Process::Thread::start(
      "diagnostics.validation/name\0x"_view, write_and_rename_file_log);
  started.visit(
      [](Process::Thread& thread) { thread.join(); },

      [&](Process::Thread::StartFailure) { EXPECT(false); });

  EXPECT_EQ(remove_log_files(initial_pattern), Count(1));
  EXPECT_EQ(remove_log_files(renamed_pattern), Count(0));
}

VALIDATION_TEST(DiagnosticsLog, file_sink_preserves_large_message) {
  constexpr auto pattern = "perimortem_diagnostics.validation.large_*.log";
  EXPECT_EQ(remove_log_files(pattern), Count(0));

  Storage::Bytes<4096> message([](Count) { return U8('x'); });
  auto started = Process::Thread::start(
      "diagnostics.validation.large"_view, write_large_file_log, &message);
  started.visit(
      [](Process::Thread& thread) { thread.join(); },

      [&](Process::Thread::StartFailure) { EXPECT(false); });

  Storage::Bytes<4608> output;
  const Count size = read_and_remove_log_file(pattern, output.get_access());
  const View::Bytes entry = output.slice(0, size);
  EXPECT(size > message.get_size());
  EXPECT_EQ(entry[size - 1], U8('\n'));

  const View::Bytes payload =
      entry.slice(size - message.get_size() - 1, message.get_size());
  EXPECT_EQ(payload, message.get_view());
}

#endif
