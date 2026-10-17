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
#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/core/view/null_terminated.hpp"
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
  return View::Bytes(allocation.get_data(), source.get_size());
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

#else

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
      Process::Thread::Function function,
      void* context,
      U64& handle)
      : requested_name(requested_name),
        function(function),
        context(context),
        sink(Diagnostics::Log::get_sink()),
        disable_log_header(Diagnostics::Log::get_disable_header()) {
    if (!function) {
      Diagnostics::Log::fatal(
          "Process::Thread requires a callback function."_view);
    }

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
      Diagnostics::Log::fatal(
          "Process::Thread failed to create a native thread."_view);
    }

    // The acquire load pairs with `mark_initialized` after dispatch copies
    // every value held in this spawning frame. Construction cannot return
    // while the new thread still refers to the initializer.
    while (Bool(__atomic_load_n(&initialized.value, __ATOMIC_ACQUIRE)) !=
           True) {
      yield_thread();
    }
  }

  ThreadInitializer(const ThreadInitializer&) = delete;
  auto operator=(const ThreadInitializer&) -> ThreadInitializer& = delete;

 private:
  auto mark_initialized() -> void {
    __atomic_store_n(&initialized.value, True.value, __ATOMIC_RELEASE);
  }

  static auto dispatch(void* initializer_address) -> void* {
    auto& initializer = *Data::cast<ThreadInitializer>(initializer_address);

    View::Bytes name = copy_thread_name(initializer.requested_name);
    Diagnostics::Log::set_thread_name(name);
    set_system_thread_name(name);

    // Diagnostic routing begins with the spawning thread's policy. The
    // callback can replace either setting without mutating that source thread.
    Diagnostics::Log::set_sink(initializer.sink);
    Diagnostics::Log::set_disable_header(initializer.disable_log_header);

    Process::Thread::Function function = initializer.function;
    void* context = initializer.context;

    {
      Diagnostics::Log::Message<64> start_message(
          Diagnostics::Log::Level::Debug);
      start_message << "Process::Thread started."_view;
    }

    initializer.mark_initialized();
    function(context);

    // Completion uses the callback's current diagnostic policy because a
    // thread can deliberately replace the inherited sink while it runs.
    {
      Diagnostics::Log::Message<64> completion_message(
          Diagnostics::Log::Level::Debug);
      completion_message << "Process::Thread completed."_view;
    }

    return nullptr;
  }

  View::Bytes requested_name;
  Process::Thread::Function function;
  void* context;
  Diagnostics::Log::Sink sink;
  Bool disable_log_header;
  Bool initialized = False;
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

auto Process::Thread::start(View::Bytes name, Function function, void* context)
    -> Thread {
  Thread thread;
  ThreadInitializer initializer(name, function, context, thread.handle);
  thread.active = True;
  return thread;
}

auto Process::Thread::join() -> void {
  if (!active) {
    return;
  }

#ifdef TOOLCHAIN_WINDOWS

  HANDLE native_thread = Data::restore<HANDLE>(CppSize(handle));
  const auto waited = WaitForSingleObject(native_thread, INFINITE);
  if (waited != WAIT_OBJECT_0) {
    Diagnostics::Log::fatal(
        "Process::Thread failed to join its native thread."_view);
  }

  CloseHandle(native_thread);

#else

  pthread_t native_thread;
  Data::copy(
      Data::cast<U8>(&native_thread), Data::cast<const U8>(&handle),
      sizeof(native_thread));
  if (pthread_join(native_thread, nullptr) != 0) {
    Diagnostics::Log::fatal(
        "Process::Thread failed to join its native thread."_view);
  }

#endif

  handle = 0;
  active = False;
}
