// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/process/thread.hpp"

#ifdef TOOLCHAIN_WINDOWS
#include <process.h>
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

#include "perimortem/core/data.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/diagnostics/message.hpp"
#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/core/writer/textual.hpp"
#include "perimortem/memory/allocator/bibliotheca.hpp"

#ifndef TOOLCHAIN_WINDOWS

static_assert(
    sizeof(pthread_t) <= sizeof(U64) && alignof(pthread_t) <= alignof(U64),
    "pthread_t does not fit the thread token storage");

#endif

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;

#ifdef TOOLCHAIN_WINDOWS

static auto yield_thread() -> void {
  SwitchToThread();
}

#else

static auto yield_thread() -> void {
  sched_yield();
}

#endif

// Startup names originate in the spawning thread. Copying them into the new
// thread's Bibliotheca keeps diagnostic storage valid until native teardown
// releases that complete thread local archive.
static auto copy_thread_name(View::Bytes source) -> View::Bytes {
  if (source.is_empty()) {
    return View::Bytes();
  }

  const auto allocation = Allocator::Bibliotheca::check_out(source.get_size());
  Data::copy(allocation.get_data(), source.get_data(), source.get_size());
  return View::Bytes(allocation.get_data()[0], source.get_size());
}

static auto set_system_thread_name(View::Bytes name) -> void {
  if (name.is_empty()) {
    return;
  }

#ifdef TOOLCHAIN_WINDOWS

  // Diagnostics retain the complete UTF-8 name. The debugger receives a
  // bounded UTF-16 prefix so native naming requires no additional allocation.
  wchar_t native_name[256];
  const S32 size = name.get_size() < 256 ? S32(name.get_size()) : 255;
  const S32 written = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, Data::cast<char>(name.get_data()), size,
      native_name, 255);
  if (written) {
    native_name[written] = 0;
    SetThreadDescription(GetCurrentThread(), native_name);
  }

#elifdef TOOLCHAIN_LINUX

  // Pthreads accept at most fifteen name bytes plus one null terminator.
  // Zero extension terminates shorter names, while the final assignment keeps
  // a clipped prefix terminated.
  Storage::Bytes<16> native_name(name);
  native_name[15] = 0;
  pthread_setname_np(pthread_self(), Data::cast<char>(native_name.get_data()));

#endif
}

class ThreadInitializer {
 public:
  ThreadInitializer(
      View::Bytes requested_name,
      Process::Thread::Function& function,
      void* context,
      U64& handle)
      : requested_name(requested_name),
        function(function),
        context(context),
        sink(Diagnostics::Log::get_sink()),
        level(Diagnostics::Log::get_level()) {

#ifdef TOOLCHAIN_WINDOWS

    handle = _beginthreadex(
        nullptr, 0,
        [](void* initializer) -> U32 {
          dispatch(initializer);
          return 0;
        },

        this, 0, nullptr);

    const bool failed = handle == 0;

#else

    pthread_t native_thread;
    const auto result = pthread_create(&native_thread, nullptr, dispatch, this);
    const bool failed = result != 0;
    if (!failed) {
      Data::copy(
          Data::cast<U8>(&handle), Data::cast<const U8>(&native_thread),
          sizeof(native_thread));
    }

#endif

    if (failed) {
      return;
    }

    // The acquire load pairs with `mark_initialized` after dispatch copies
    // every value held in this spawning frame. Construction cannot return
    // while the new thread still refers to the initializer.
    while (Bool(__atomic_load_n(&initialized.value, __ATOMIC_ACQUIRE)) !=
           True) {
      yield_thread();
    }

    started = True;
  }

  ThreadInitializer(const ThreadInitializer&) = delete;
  auto operator=(const ThreadInitializer&) -> ThreadInitializer& = delete;

 private:
  friend class Process::Thread;

  auto has_started() const -> Bool { return started; }

  auto mark_initialized() -> void {
    __atomic_store_n(&initialized.value, True.value, __ATOMIC_RELEASE);
  }

  static auto dispatch(void* initializer_address) -> void* {
    auto& initializer = *Data::cast<ThreadInitializer>(initializer_address);

    View::Bytes name = copy_thread_name(initializer.requested_name);
    Diagnostics::Log::set_thread_name(name);
    set_system_thread_name(name);

    // Diagnostic routing begins with the spawning thread's policy. The
    // callback can replace any setting without mutating that source thread.
    Diagnostics::Log::set_sink(initializer.sink);
    Diagnostics::Log::set_level(initializer.level);

    Process::Thread::Function& function = initializer.function;
    void* context = initializer.context;

    {
      Diagnostics::Message<64> start_message(Diagnostics::Log::Level::Debug);
      start_message << "Process::Thread started."_view;
    }

    initializer.mark_initialized();
    function(context);

    // Completion uses the callback's current diagnostic policy because a
    // thread can deliberately replace the inherited sink while it runs.
    {
      Diagnostics::Message<64> completion_message(
          Diagnostics::Log::Level::Debug);
      completion_message << "Process::Thread completed."_view;
    }

    return nullptr;
  }

  View::Bytes requested_name;
  Process::Thread::Function& function;
  void* context;
  Diagnostics::Log::Sink sink;
  Diagnostics::Log::Level level;
  Bool initialized = False;
  Bool started = False;
};

Process::Thread::Thread(Thread&& source)
    : handle(source.handle), active(source.active) {
  source.handle = 0;
  source.active = False;
}

Process::Thread::~Thread() {
  join();
}

auto Process::Thread::operator=(Thread&& source) -> Thread& {
  if (this == &source) {
    return *this;
  }

  join();
  handle = source.handle;
  active = source.active;
  source.handle = 0;
  source.active = False;
  return *this;
}

auto Process::Thread::start(View::Bytes name, Function& function, void* context)
    -> Storage::Result<Thread, StartFailure> {
  Thread thread;
  ThreadInitializer initializer(name, function, context, thread.handle);
  if (!initializer.has_started()) {
    return StartFailure::HostRejected;
  }

  thread.active = True;
  return Data::take(thread);
}

auto Process::Thread::join() -> void {
  if (!active) {
    return;
  }

#ifdef TOOLCHAIN_WINDOWS

  // `_beginthreadex` returns a thread HANDLE through `uintptr_t`. Thread
  // retains that exact representation until join supplies it to
  // WaitForSingleObject and CloseHandle. No other operation interprets it.
  HANDLE native_thread = Data::bit_cast<HANDLE>(handle);
  const auto waited = WaitForSingleObject(native_thread, INFINITE);
  const Bool joined = waited == WAIT_OBJECT_0;
  if (joined && !CloseHandle(native_thread)) {
    const U32 close_error = GetLastError();
    Diagnostics::Message<256> failure(Diagnostics::Log::Level::Error);
    failure << "Process::Thread finished its callback but could not release "
               "the native handle. Execution can continue, but the handle may "
               "remain until process exit. error="_view
            << close_error;
  }

  const U64 join_error = waited == WAIT_FAILED ? GetLastError() : U64(waited);

#else

  pthread_t native_thread;
  Data::copy(
      Data::cast<U8>(&native_thread), Data::cast<const U8>(&handle),
      sizeof(native_thread));

  const S32 join_error = pthread_join(native_thread, nullptr);
  const Bool joined = join_error == 0;

#endif

  // A failed wait leaves callback execution and caller context lifetime
  // unresolved. Returning would allow destruction to make that context
  // inaccessible while the native thread may still use it, so Thread cannot
  // publish a recoverable state.
  if (!joined) {
    Storage::Bytes<320> diagnostic;
    Writer::Textual message(diagnostic);
    message
        << "Process::Thread cannot guarantee its callback stopped after the "
           "native join failed. Terminating before caller context can become "
           "inaccessible. Check for native handle corruption or concurrent "
           "join calls. error="_view
        << join_error;
    Diagnostics::Log::fatal(diagnostic.slice(0, message.get_location()));
  }

  handle = 0;
  active = False;
}
