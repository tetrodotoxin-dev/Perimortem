// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/diagnostics/log.hpp"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef TOOLCHAIN_LINUX
#include <dlfcn.h>
#include <execinfo.h>
#endif

#include "perimortem/core/data.hpp"
#include "perimortem/core/diagnostics/time.hpp"
#include "perimortem/core/math/scalar.hpp"
#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/core/storage/vector.hpp"
#include "perimortem/core/synchronization/mutex.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/core/writer/textual.hpp"

using namespace Perimortem::Core;

const Diagnostics::Log::Sink Diagnostics::Log::default_sink =
    Diagnostics::Log::file_sink;

constexpr Count max_file_label_size = 64;
constexpr Count source_path_suffix_size = 100;
constexpr auto empty_message = "<EMPTY MESSAGE>"_view;

#ifdef TOOLCHAIN_LINUX

constexpr Count max_backtrace_name_size = 2048;

#endif

static Synchronization::Mutex output_mutex;
static thread_local View::Bytes thread_name = "main"_view;
static U64 file_sequence = 0;

static constexpr auto normalize_message(View::Bytes message) -> View::Bytes {
  return message.is_empty() ? empty_message : message;
}

static constexpr auto is_file_label_byte(U8 value) -> Bool {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
         (value >= '0' && value <= '9') || value == '.' || value == '_' ||
         value == '-';
}

static auto write_file_label(Writer::Textual& writer, View::Bytes name)
    -> void {
  const Count size = Math::Scalar::min(name.get_size(), max_file_label_size);
  if (size == 0) {
    writer << "unnamed"_view;
    return;
  }

  for (Count index = 0; index < size; index++) {
    if (is_file_label_byte(name[index])) {
      writer << name.slice(index, 1);
    } else {
      writer << '_';
    }
  }
}

static auto write_message(FILE* output, View::Bytes message) -> void {
  message = normalize_message(message);
  fwrite(message.get_data(), 1, message.get_size(), output);
  if (message[message.get_size() - 1] != '\n') {
    constexpr char newline = '\n';
    fwrite(&newline, 1, 1, output);
  }
}

// ThreadWriter creates no artifact until the thread publishes its first entry.
// That entry permanently selects either one file or `color_sink` for the rest
// of the thread. Opening uses replacement semantics, so an existing file with
// the same generated name is overwritten rather than treated as a collision.
class ThreadWriter {
 public:
  ~ThreadWriter() {
    if (file) {
      fflush(file);
      fclose(file);
      file = nullptr;
    }
  }

  auto accumulate(View::Bytes header, View::Bytes message) -> Bool {
    if (!file_ready) {
      prepare_file();
    }

    if (!file) {
      return False;
    }

    fwrite(header.get_data(), 1, header.get_size(), file);
    write_message(file, message);
    return True;
  }

  auto flush() -> void {
    if (file) {
      fflush(file);
    }
  }

 private:
  auto prepare_file() -> void {
    Storage::Bytes<192> name_buffer;
    Writer::Textual name(name_buffer.get_access().slice(0, 191));

    // Display names can contain arbitrary diagnostic text. A bounded portable
    // alphabet produces one usable host filename without changing the complete
    // display name retained by the thread.
    name << "perimortem_"_view;
    write_file_label(name, thread_name);

    const U64 sequence =
        __atomic_fetch_add(&file_sequence, U64(1), __ATOMIC_RELAXED);
    name << '_' << Diagnostics::Time::now().get_nanoseconds() << '_' << sequence
         << ".log"_view;
    name_buffer[name.get_location()] = '\0';

    errno = 0;
    file = fopen(Data::cast<char>(name_buffer.get_data()), "wb");

    const S32 open_error = errno;
    file_ready = True;
    if (file) {
      return;
    }

    Storage::Bytes<384> diagnostic_buffer;
    Writer::Textual diagnostic(diagnostic_buffer.get_access());
    diagnostic << "Diagnostics::Log could not create log file \""_view
               << name_buffer.slice(0, name.get_location())
               << "\". Later file diagnostics will use color_sink. errno="_view
               << open_error;
    Diagnostics::Log::color_sink(
        Diagnostics::Log::Level::Error,
        diagnostic_buffer.slice(0, diagnostic.get_location()),
        Diagnostics::Source());
  }

  FILE* file = nullptr;
  Bool file_ready = False;
};

static thread_local Diagnostics::Log::Sink message_sink =
    Diagnostics::Log::default_sink;
static thread_local Diagnostics::Log::Level thread_log_level =
    Diagnostics::Log::Level::Info;
static thread_local Diagnostics::Source attribution_override;
static thread_local ThreadWriter thread_writer;

static auto flush_builtin_outputs() -> void {
  Synchronization::Mutex::Lock lock(output_mutex);
  thread_writer.flush();
  fflush(stdout);
  fflush(stderr);
}

static auto send_to_sink(
    Diagnostics::Log::Level level,
    View::Bytes message,
    const Diagnostics::Source& location) -> void {
  if (level < thread_log_level || !message_sink) {
    return;
  }

  const Diagnostics::Source& target_source =
      attribution_override.is_set() ? attribution_override : location;
  message_sink(level, normalize_message(message), target_source);
}

// `log` publishes the Fatal message before entering this terminal path. Linux
// backtrace frames reuse the selected Sink without starting termination again.
[[noreturn]] static auto terminate_fatal() -> void {
  flush_builtin_outputs();

#ifdef TOOLCHAIN_LINUX

  Storage::Vector<void*, 64> frames;
  const S32 frame_count = backtrace(frames.get_data(), frames.get_size());
  for (S32 index = 0; index < frame_count; index++) {
    Dl_info symbol;
    const S32 resolved = dladdr(frames[index], &symbol);
    if (resolved && symbol.dli_sname) {
      send_to_sink(
          Diagnostics::Log::Level::Fatal,
          View::NullTerminated(symbol.dli_sname, max_backtrace_name_size)
              .get_view(),
          Diagnostics::Source());
    } else {
      send_to_sink(
          Diagnostics::Log::Level::Fatal, "Unknown backtrace frame."_view,
          Diagnostics::Source());
    }
  }

  flush_builtin_outputs();

#endif

  abort();
}

constexpr auto level_char(Diagnostics::Log::Level level) -> char {
  switch (level) {
  case Diagnostics::Log::Level::Debug:
    return 'D';
  case Diagnostics::Log::Level::Info:
    return 'I';
  case Diagnostics::Log::Level::Warning:
    return 'W';
  case Diagnostics::Log::Level::Error:
    return 'E';
  case Diagnostics::Log::Level::Fatal:
    return 'F';
  }

  return '?';
}

constexpr auto level_color(Diagnostics::Log::Level level) -> View::Bytes {
  switch (level) {
  case Diagnostics::Log::Level::Debug:
    return "\x1b[38;5;246m"_view;
  case Diagnostics::Log::Level::Info:
    return ""_view;
  case Diagnostics::Log::Level::Warning:
    return "\x1b[38;5;220m"_view;
  case Diagnostics::Log::Level::Error:
  case Diagnostics::Log::Level::Fatal:
    return "\x1b[38;5;160m"_view;
  }

  return ""_view;
}

static auto write_padded(Writer::Textual& writer, U64 value, Count width)
    -> void {
  U64 threshold = 1;
  for (Count digit = 1; digit < width; digit++) {
    threshold *= 10;
  }

  while (threshold > 1 && value < threshold) {
    writer << '0';
    threshold /= 10;
  }

  writer << value;
}

static auto write_source_path(Writer::Textual& writer, View::Bytes path)
    -> void {
  if (path.get_size() <= source_path_suffix_size) {
    writer << path;
    return;
  }

  writer << "..."_view;
  writer << path.slice(path.get_size() - source_path_suffix_size);
}

Diagnostics::Log::Attribution::Attribution(Attribution&& rhs) {
  primary_guard = rhs.primary_guard;
  rhs.primary_guard = False;
}

Diagnostics::Log::Attribution::~Attribution() {
  if (primary_guard) {
    attribution_override = Source();
  }
}

auto Diagnostics::Log::file_sink(
    Level level,
    View::Bytes message,
    const Source& location) -> void {
  Storage::Bytes<header_capacity> header_buffer;
  const View::Bytes header = generate_header(level, location, header_buffer);
  if (!thread_writer.accumulate(header, message)) {
    color_sink(level, message, location);
  }
}

auto Diagnostics::Log::plain_sink(
    Level level,
    View::Bytes message,
    const Source&) -> void {
  Synchronization::Mutex::Lock lock(output_mutex);
  FILE* stream = (level >= Level::Error) ? stderr : stdout;
  write_message(stream, message);
  fflush(stream);
}

auto Diagnostics::Log::color_sink(
    Level level,
    View::Bytes message,
    const Source& location) -> void {
  Storage::Bytes<header_capacity> header_buffer;
  const View::Bytes header = generate_header(level, location, header_buffer);
  const View::Bytes color = level_color(level);
  constexpr auto reset = "\x1b[0m"_view;

  Synchronization::Mutex::Lock lock(output_mutex);
  FILE* stream = (level >= Level::Error) ? stderr : stdout;
  fwrite(color.get_data(), 1, color.get_size(), stream);
  fwrite(header.get_data(), 1, header.get_size(), stream);
  write_message(stream, message);
  fwrite(reset.get_data(), 1, reset.get_size(), stream);
  fflush(stream);
}

auto Diagnostics::Log::stderr_sink(
    Level level,
    View::Bytes message,
    const Source& location) -> void {
  Storage::Bytes<header_capacity> header_buffer;
  const View::Bytes header = generate_header(level, location, header_buffer);

  Synchronization::Mutex::Lock lock(output_mutex);
  fwrite(header.get_data(), 1, header.get_size(), stderr);
  write_message(stderr, message);
  fflush(stderr);
}

auto Diagnostics::Log::debug_sink(
    Level level,
    View::Bytes message,
    const Source& location) -> void {
  color_sink(level, message, location);
  file_sink(level, message, location);
}

auto Diagnostics::Log::set_sink(Sink sink) -> void {
  message_sink = sink;
}

auto Diagnostics::Log::get_sink() -> Sink {
  return message_sink;
}

auto Diagnostics::Log::set_thread_name(View::Bytes name) -> void {
  thread_name = name;
}

auto Diagnostics::Log::set_level(Level level) -> void {
  thread_log_level = level;
}

auto Diagnostics::Log::get_level() -> Diagnostics::Log::Level {
  return thread_log_level;
}

auto Diagnostics::Log::set_attribution(const Source& location) -> Attribution {
  // Preserve the outermost override so nested helpers cannot replace the source
  // location selected by their caller.
  Attribution scope_guard;
  scope_guard.primary_guard = !attribution_override.is_set();
  if (scope_guard.primary_guard) {
    attribution_override = location;
  }

  return scope_guard;
}

auto Diagnostics::Log::log(
    Level level,
    View::Bytes message,
    const Source& location) -> void {
  send_to_sink(level, message, location);
  if (level == Level::Fatal) {
    terminate_fatal();
  }
}

auto Diagnostics::Log::generate_header(
    Log::Level level,
    const Source& location,
    Storage::Bytes<header_capacity>& buffer) -> View::Bytes {
  Writer::Textual writer(buffer.get_access());
  writer << level_char(level) << ' ';

  // Diagnostic time exposes spacing between messages without relying on wall
  // time. A 100 microsecond reporting floor avoids promising precision beyond
  // the practical overhead of producing a diagnostic.
  const auto current = Diagnostics::Time::now();
  const U64 elapsed_100_microseconds =
      Diagnostics::Time::boot().measure(current).convert_to_nanoseconds() /
      100'000;

  const U64 elapsed_milliseconds = elapsed_100_microseconds / 10;
  const U64 elapsed_seconds = elapsed_milliseconds / 1'000;
  write_padded(writer, elapsed_seconds / 60, 2);
  writer << ':';
  write_padded(writer, elapsed_seconds % 60, 2);
  writer << ':';
  write_padded(writer, elapsed_milliseconds % 1'000, 3);
  writer << '.';
  writer << elapsed_100_microseconds % 10 << ' ';
  writer << '['
         << thread_name.slice(
                0, Math::Scalar::min(thread_name.get_size(), Count(64)))
         << "] "_view;

  if (location.is_set()) {
    write_source_path(writer, location.get_file());
    writer << ':' << location.get_line();
    writer << ':' << location.get_column();
    writer << ": "_view;
  }

  return buffer.slice(0, writer.get_location());
}

auto Diagnostics::Log::debug(View::Bytes message, const Source& location)
    -> void {
  log(Level::Debug, message, location);
}

auto Diagnostics::Log::info(View::Bytes message, const Source& location)
    -> void {
  log(Level::Info, message, location);
}

auto Diagnostics::Log::warning(View::Bytes message, const Source& location)
    -> void {
  log(Level::Warning, message, location);
}

auto Diagnostics::Log::error(View::Bytes message, const Source& location)
    -> void {
  log(Level::Error, message, location);
}

auto Diagnostics::Log::fatal(View::Bytes message, const Source& location)
    -> void {
  log(Level::Fatal, message, location);
  __builtin_unreachable();
}
