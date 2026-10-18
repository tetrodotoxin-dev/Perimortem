// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/diagnostics/source.hpp"
#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/core/view/bytes.h"

namespace Perimortem::Core::Diagnostics {

// Log separates diagnostic production from its final destination. Runtime
// systems publish through one synchronous boundary while each thread applies
// its own minimum level and selected Sink. `file_sink` writes to a separate
// file for each thread by default. `set_sink` replaces that routing without
// changing diagnostic producers.
//
// Log invokes the selected Sink before returning. A Sink borrows the message
// and Source for that callback and copies any data it retains. A Sink shared by
// multiple threads serializes its mutable state. The supplied shared sinks use
// Core::Synchronization::Mutex, while `file_sink` needs no lock because each
// thread has its own file. A null Sink drops nonfatal diagnostics. Fatal still
// terminates the process.
class Log {
 public:
  // Levels increase in severity. Filtering forwards the selected threshold and
  // every level after it. The supplied terminal sinks use Error as the boundary
  // between stdout and stderr.
  enum class Level : U8 {
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
  };

  // Attribution applies one caller-selected Source to every nested diagnostic
  // on the current thread. Only the outermost live guard installs a Source, so
  // helper scopes cannot replace the original context. Moving the guard
  // transfers responsibility for ending that override. An explicitly authored
  // Source continues borrowing its file and function Views until that outermost
  // guard ends.
  class Attribution {
    friend Log;

   public:
    EXPORTED(PERIMORTEM) Attribution(Attribution&&);
    EXPORTED(PERIMORTEM) ~Attribution();

   private:
    Attribution() = default;

    Bool primary_guard = False;
  };

  // Receives one unformatted message and its resolved Source synchronously. A
  // custom Sink completes any required persistence before returning because Log
  // has no separate callback for flushing external state.
  using Sink =
      void (*)(Level level, Core::View::Bytes message, const Source& location);

  // Persists canonical entries in one file selected on first use. Its filename
  // combines at most 64 display-name bytes with monotonic time and a process
  // sequence. ASCII letters, digits, periods, underscores and hyphens are
  // preserved while every other byte becomes an underscore. A collision with a
  // file outside the current process overwrites that file. If the file cannot
  // be opened, `file_sink` reports that failure through `color_sink` and
  // permanently sends that thread's later entries there.
  static EXPORTED(PERIMORTEM) auto file_sink(
      Level level,
      Core::View::Bytes message,
      const Source& location) -> void;

  // Writes the message without a Perimortem header or Source. Debug, Info and
  // Warning select stdout while Error and Fatal select stderr. The shared
  // output Mutex keeps the complete message and its terminating newline
  // contiguous with respect to every other supplied terminal Sink and flushes
  // the selected stream before releasing the lock.
  static EXPORTED(PERIMORTEM) auto plain_sink(
      Level level,
      Core::View::Bytes message,
      const Source& location) -> void;

  // Writes one canonical entry wrapped in an ANSI color selected by level.
  // Debug is grey, Warning is yellow, Error and Fatal are red, and Info retains
  // the terminal's current color. Severity selects stdout or stderr using the
  // same boundary as `plain_sink`. The shared output Mutex keeps the header,
  // message, newline and color reset contiguous and flushes the selected stream
  // before releasing the lock.
  static EXPORTED(PERIMORTEM) auto color_sink(
      Level level,
      Core::View::Bytes message,
      const Source& location) -> void;

  // Writes one canonical entry to stderr and flushes before returning. Protocol
  // processes use this sink to keep stdout free for their transport. The shared
  // output Mutex keeps the complete entry contiguous with terminal diagnostics
  // from other threads.
  static EXPORTED(PERIMORTEM) auto stderr_sink(
      Level level,
      Core::View::Bytes message,
      const Source& location) -> void;

  // Sends each entry to both `color_sink` and `file_sink`, preserving
  // interactive output together with a persistent record.
  static EXPORTED(PERIMORTEM) auto debug_sink(
      Level level,
      Core::View::Bytes message,
      const Source& location) -> void;

  // Selects the Sink invoked by later messages on this thread. A
  // System::Process::Thread copies its spawning thread's selection during
  // startup. Other native threads begin with `default_sink`. A null Sink drops
  // nonfatal messages without preventing Fatal termination.
  static EXPORTED(PERIMORTEM) auto set_sink(Sink sink) -> void;
  static EXPORTED(PERIMORTEM) auto get_sink() -> Sink;

  // Sets the display name written by later diagnostics on the current thread.
  // The caller keeps the supplied View storage valid until the name is replaced
  // or the thread exits. Canonical headers and file labels use at most the
  // first 64 bytes. Changing the name after the first file entry does not
  // retarget the open file.
  static EXPORTED(PERIMORTEM) auto set_thread_name(Core::View::Bytes name)
      -> void;

  // Sets the minimum level that will be forwarded to the Sink on this thread.
  // Messages below this level are dropped before formatting.
  static EXPORTED(PERIMORTEM) auto set_level(Level level) -> void;
  static EXPORTED(PERIMORTEM) auto get_level() -> Diagnostics::Log::Level;

  // Establishes Source attribution until the returned guard leaves scope.
  // Nested calls retain the outermost attributed location.
  static EXPORTED(PERIMORTEM) auto set_attribution(
      const Source& location = Source::current()) -> Attribution;

  // Names `file_sink` as the stable callback used to restore default routing
  // after a caller temporarily installs another Sink.
  static EXPORTED(PERIMORTEM) const Sink default_sink;

  // Applies the calling thread's threshold, resolves its outermost attributed
  // Source and invokes its Sink. An empty message becomes `<EMPTY MESSAGE>`.
  // Level::Fatal enters the termination path after the Sink returns, even when
  // filtering or a null Sink prevents publication.
  static EXPORTED(PERIMORTEM) auto log(
      Level level,
      Core::View::Bytes message,
      const Source& location) -> void;

  // The named severity entry points capture the caller's Source during constant
  // evaluation and then use the same filtering, attribution and Sink path.
  static EXPORTED(PERIMORTEM) auto debug(
      Core::View::Bytes message,
      const Source& location = Source::current()) -> void;
  static EXPORTED(PERIMORTEM) auto info(
      Core::View::Bytes message,
      const Source& location = Source::current()) -> void;
  static EXPORTED(PERIMORTEM) auto warning(
      Core::View::Bytes message,
      const Source& location = Source::current()) -> void;
  static EXPORTED(PERIMORTEM) auto error(
      Core::View::Bytes message,
      const Source& location = Source::current()) -> void;

  // Submits Level::Fatal through `log`. The termination path flushes
  // Perimortem's thread file together with the C stdout and stderr streams,
  // attempts to report a Linux backtrace, then always aborts. A null Sink does
  // not prevent termination. The `[[noreturn]]` declaration keeps that control
  // flow visible to the compiler.
  [[noreturn]] static EXPORTED(PERIMORTEM) auto fatal(
      Core::View::Bytes message,
      const Source& location = Source::current()) -> void;

  // The 256 byte header bound holds the complete severity and elapsed time
  // fields, the first 64 bytes of the display name, an ellipsis followed by the
  // final 100 Source path bytes and the full decimal range of both Count
  // location coordinates.
  static constexpr Count header_capacity = 256;

  // Generates the canonical prefix shared by formatted sinks. Elapsed time uses
  // MM:SS:mmm.t with a 100 microsecond reporting floor and expanding minutes.
  // Display names retain their first 64 bytes. Source paths longer than 100
  // bytes retain their final 100 bytes preceded by `...`. An unset Source omits
  // the location fields. The returned View borrows `buffer` until that buffer
  // is modified or leaves scope.
  static EXPORTED(PERIMORTEM) auto generate_header(
      Level level,
      const Source& location,
      Core::Storage::Bytes<header_capacity>& buffer) -> Core::View::Bytes;
};

}  // namespace Perimortem::Core::Diagnostics
