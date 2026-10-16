// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/process/worker.hpp"

#ifdef TOOLCHAIN_WINDOWS
#include <process.h>
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

#include "perimortem/core/algorithm/search.hpp"
#include "perimortem/core/data.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/core/storage/vector.hpp"
#include "perimortem/memory/allocator/bibliotheca.hpp"
#include "perimortem/system/mutex.hpp"

#ifndef TOOLCHAIN_WINDOWS

static_assert(
    sizeof(pthread_t) <= sizeof(U64) && alignof(pthread_t) <= alignof(U64),
    "pthread_t does not fit the worker token storage");

#endif

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;

// Perimortem workers publish their reserved identity and copied name here. A
// native thread created elsewhere retains the all bits set identity and an
// empty name, which worker queries treat as the main thread state.
static thread_local U64 this_thread_id = Count(-1);
static thread_local View::Bytes this_thread_name = ""_view;

using WorkerJobFunction = Process::Worker::JobFunction;

static Mutex worker_mutex;

#ifdef TOOLCHAIN_WINDOWS

static auto yield_worker() -> void {
  SwitchToThread();
}

#else

static auto yield_worker() -> void {
  sched_yield();
}

#endif

// ThreadInitializer keeps the startup arguments in the spawning frame until the
// new thread copies every source View into thread local storage. Construction
// blocks until that copy completes, establishing the same lifetime boundary for
// every job.
class ThreadInitializer {
 public:
  ThreadInitializer(
      View::Bytes requested_name,
      WorkerJobFunction job_function,
      View::Bytes job_data,
      U64& handle)
      : requested_name(requested_name),
        job_function(job_function),
        job_data(job_data),
        sink(Diagnostics::Log::get_sink()),
        disable_log_header(Diagnostics::Log::get_disable_header()) {
    if (!job_function) {
      Diagnostics::Log::fatal(
          "Process::Worker requested to start without a job function."_view);
    }

    // Reserve the logical identity before creating its native thread. A native
    // creation failure releases both parts of that transaction.
    thread_id = reserve_thread();

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

    pthread_t thread;
    const auto pthread_result =
        pthread_create(&thread, nullptr, dispatch, this);
    const bool failed = pthread_result != 0;
    if (!failed) {
      Data::copy(Data::cast<U8>(&handle), &thread, 1);
    }

#endif

    if (failed) {
      release_thread(thread_id);
      Diagnostics::Log::fatal(
          "Process::Worker failed to create a native thread."_view);
    }

    // The acquire load pairs with `mark_initialized` after dispatch copies
    // every field it needs. Construction cannot return while the worker still
    // refers to this spawning frame.
    while (Bool(__atomic_load_n(&initialized.value, __ATOMIC_ACQUIRE)) !=
           True) {
      yield_worker();
    }
  };

  // Copying would duplicate synchronization state for one spawned thread, so
  // the initializer remains tied to its spawning frame.
  ThreadInitializer(const ThreadInitializer&) = delete;
  auto operator=(const ThreadInitializer&) -> ThreadInitializer& = delete;

  static auto get_worker_count() -> Count {
    Mutex::Lock lock(worker_mutex);

    Count worker_count = 0;
    for (Count i = 0; i < thread_occupancy.get_size(); i++) {
      worker_count += thread_occupancy[i] != 0 ? 1 : 0;
    }

    return worker_count;
  }

 private:
  // Signals that the worker copied every source View into its thread local
  // storage. The spawning constructor can then release the initializer, while
  // the copied name and job data remain valid through the worker job.
  auto mark_initialized() -> void {
    __atomic_store_n(&initialized.value, True.value, __ATOMIC_RELEASE);
  }

  // A zero in `thread_occupancy` marks a free worker identity. Selection and
  // reservation require a lock on `worker_mutex`, so concurrent worker creation
  // cannot claim the same identity. `Search::min` returns the lowest zero index
  // when one exists. If `thread_occupancy[worker_index]` is one, every worker
  // identity is reserved and the process aborts.
  static auto reserve_thread() -> Count {
    Mutex::Lock lock(worker_mutex);

    auto worker_index =
        Algorithm::Search::min(View::Vector<U8>(thread_occupancy));
    if (thread_occupancy[worker_index] != 0) {
      Diagnostics::Log::fatal(
          "Program requested to create a Process::Worker but all 64 Workers "
          "were already occupied."_view);
    }

    thread_occupancy[worker_index] = 1;
    return worker_index;
  }

  static auto release_thread(Count worker_index) -> void {
    Mutex::Lock lock(worker_mutex);
    thread_occupancy[worker_index] = 0;
  }

  // Copies startup bytes into the worker thread's Bibliotheca. The returned
  // View remains valid through the job. Thread local teardown releases its
  // storage when the native thread exits. Empty input requires no allocation.
  static auto copy_thread_bytes(View::Bytes source_bytes) -> View::Bytes {
    if (source_bytes.is_empty()) {
      return View::Bytes();
    }

    const auto allocation =
        Allocator::Bibliotheca::check_out(source_bytes.get_size());
    Data::copy(
        allocation.get_data(), source_bytes.get_data(),
        source_bytes.get_size());
    return View::Bytes(allocation.get_data(), source_bytes.get_size());
  }

  static auto set_system_thread_name(View::Bytes thread_name) -> void {
    if (thread_name.is_empty()) {
      return;
    }

#ifdef TOOLCHAIN_WINDOWS

    // The full name remains in our thread storage. The debugger name uses a
    // bounded native buffer so naming never needs another allocation.
    wchar_t name[256];
    const S32 size =
        thread_name.get_size() < 256 ? S32(thread_name.get_size()) : 255;
    const S32 written = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, Data::cast<char>(thread_name.get_data()),
        size, name, 255);
    if (written) {
      name[written] = 0;
      SetThreadDescription(GetCurrentThread(), name);
    }

#else

    // Max name length for pthreads is 16 (including the null terminator)
    // Storage::Bytes zero extends for names shorter than 16.
    Storage::Bytes<16> system_thread_name(thread_name);

    // Pthreads accept at most fifteen name bytes plus the terminator. Forcing
    // the final byte to zero preserves termination when the full name was
    // clipped.
    system_thread_name[15] = 0;

    pthread_setname_np(
        pthread_self(), Data::cast<char>(system_thread_name.get_data()));

#endif
  }

  static auto dispatch(void* initializer_address) -> void* {
    // The logical identity is reserved under `worker_mutex` before native
    // dispatch. Only the new thread can publish it to this thread local slot,
    // so the assignment needs no additional synchronization. Jobs, diagnostics
    // and current thread queries then share the same worker identity.
    auto& initializer = *Data::cast<ThreadInitializer>(initializer_address);
    this_thread_id = initializer.thread_id;

    // Perimortem retains the complete name for diagnostics. The native thread
    // name is a separate bounded copy used by platform debuggers and tracing.
    this_thread_name = copy_thread_bytes(initializer.requested_name);
    Diagnostics::Log::set_thread_name(this_thread_name);
    set_system_thread_name(this_thread_name);

    // The initial sink and header policy match the spawning thread. A job may
    // replace either setting for later messages without changing the spawning
    // thread's local diagnostic state.
    Diagnostics::Log::set_sink(initializer.sink);
    Diagnostics::Log::set_disable_header(initializer.disable_log_header);

    // The function pointer is copied and the job bytes move into thread local
    // storage before `mark_initialized` lets the spawning frame disappear.
    WorkerJobFunction job_function = initializer.job_function;
    View::Bytes job_data = copy_thread_bytes(initializer.job_data);

    // Emit the start event through the inherited sink before the job can
    // replace it. Successful initialization therefore produces one diagnostic
    // record containing the reserved identity and copied payload size.
    {
      Diagnostics::Log::Message<128> start_message(
          Diagnostics::Log::Level::Debug);
      start_message << "Process::Worker spawned worker_index="_view
                    << this_thread_id << " job_bytes="_view
                    << job_data.get_size();
    }

    initializer.mark_initialized();
    job_function(job_data);

    // A job may choose a different logger while it runs. Report completion
    // through the logger currently installed for this thread rather than
    // restoring the spawning thread's logger state.
    {
      Diagnostics::Log::Message<128> exit_message(
          Diagnostics::Log::Level::Debug);
      exit_message << "Process::Worker completed worker_index="_view
                   << this_thread_id;
    }

    // All code associated with the logical worker has finished. Returning after
    // releasing its slot ends the native thread and tears down thread local
    // storage.
    release_thread(this_thread_id);
    return nullptr;
  }

  Count thread_id = 0;
  View::Bytes requested_name{};
  WorkerJobFunction job_function;
  View::Bytes job_data{};
  Diagnostics::Log::Sink sink;
  Bool disable_log_header;
  Bool initialized = False;

  static inline Storage::Vector<U8, Process::Worker::max_workers()>
      thread_occupancy;
};

Process::Worker::Worker(Worker&& other_worker) : handle(other_worker.handle) {
  other_worker.handle = 0;
}

Process::Worker::~Worker() {
  // Destruction joins the Worker's native thread before releasing its token.
  join();
}

auto Process::Worker::operator=(Worker&& other_worker) -> Worker& {
  if (this == &other_worker) {
    return *this;
  }

  join();
  handle = other_worker.handle;
  other_worker.handle = 0;
  return *this;
}

auto Process::Worker::join() -> void {
  if (handle) {

#ifdef TOOLCHAIN_WINDOWS

    HANDLE thread = Data::restore<HANDLE>(CppSize(handle));
    const auto waited = WaitForSingleObject(thread, INFINITE);
    if (waited != WAIT_OBJECT_0) {
      Diagnostics::Log::fatal(
          "Process::Worker failed to join its thread."_view);
    }

    CloseHandle(thread);

#else

    pthread_t thread;
    Data::copy(
        Data::cast<U8>(&thread), Data::cast<const U8>(&handle), sizeof(thread));
    pthread_join(thread, nullptr);

#endif

    handle = 0;
  }
}

auto Process::Worker::start(
    View::Bytes name,
    JobFunction job_function,
    View::Bytes job_data) -> Process::Worker {
  Process::Worker worker;
  ThreadInitializer initializer(name, job_function, job_data, worker.handle);
  return worker;
}

auto Process::Worker::on_main_thread() -> Bool {
  return this_thread_id == Count(-1);
}

auto Process::Worker::get_thread_id() -> Count {
  return Count(this_thread_id);
}

auto Process::Worker::get_thread_name() -> View::Bytes {
  if (on_main_thread()) {
    return "main"_view;
  }

  return this_thread_name;
}

auto Process::Worker::get_worker_count() -> Count {
  return ThreadInitializer::get_worker_count();
}
