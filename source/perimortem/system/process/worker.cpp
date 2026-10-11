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
#include "perimortem/core/static/bytes.hpp"
#include "perimortem/core/static/vector.hpp"
#include "perimortem/memory/allocator/bibliotheca.hpp"

#ifndef TOOLCHAIN_WINDOWS

static_assert(
    sizeof(pthread_t) <= sizeof(U64) && alignof(pthread_t) <= alignof(U64),
    "pthread_t does not fit the worker token storage");

#endif

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;

using Process::Worker;

// Stores the logical thread id.
// A thread created outside Perimortem uses the negative one id and represents
// the main thread for worker queries.
static thread_local U64 this_thread_id = Count(-1);
static thread_local View::Bytes this_thread_name = ""_view;

using WorkerJobFunction = Process::Worker::JobFunction;

#ifdef TOOLCHAIN_WINDOWS

static SRWLOCK worker_mutex = SRWLOCK_INIT;
static auto lock_workers() -> void {
  AcquireSRWLockExclusive(&worker_mutex);
}

static auto unlock_workers() -> void {
  ReleaseSRWLockExclusive(&worker_mutex);
}

static auto yield_worker() -> void {
  SwitchToThread();
}

#else

static pthread_mutex_t worker_mutex = PTHREAD_MUTEX_INITIALIZER;
static auto lock_workers() -> void {
  pthread_mutex_lock(&worker_mutex);
}

static auto unlock_workers() -> void {
  pthread_mutex_unlock(&worker_mutex);
}

static auto yield_worker() -> void {
  sched_yield();
}

#endif

// ThreadInitializer owns the full worker startup transaction.
// Constructing it blocks the host thread long enough for the spawned thread to
// copy every caller owned view into its own thread local storage. The
// constructor establishes that lifetime rule for every job.
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
      Data::copy(reinterpret_cast<U8*>(&handle), &thread, 1);
    }

#endif

    if (failed) {
      release_thread(thread_id);
      Diagnostics::Log::fatal(
          "Process::Worker failed to create a native thread."_view);
    }

    // Block until the spawned thread marks itself as initialized.
    // This blocks the host thread until all required data is safely copied.
    while (Bool(__atomic_load_n(&initialized.value, __ATOMIC_ACQUIRE)) !=
           True) {
      yield_worker();
    }
  };

  // The spawning frame uniquely owns its initialization transaction.
  ThreadInitializer(const ThreadInitializer&) = delete;
  auto operator=(const ThreadInitializer&) -> ThreadInitializer& = delete;

  static auto get_worker_count() -> Count {
    lock_workers();

    Count worker_count = 0;
    for (Count i = 0; i < thread_occupancy.get_size(); i++) {
      worker_count += thread_occupancy[i] != 0 ? 1 : 0;
    }

    unlock_workers();
    return worker_count;
  }

 private:
  // Signals that the worker copied every source View into its thread local
  // storage. The spawning constructor can then release the initializer, while
  // the copied name and job data remain valid through the worker job.
  auto mark_initialized() -> void {
    __atomic_store_n(&initialized.value, True.value, __ATOMIC_RELEASE);
  }

  // Reserves a thread slot for a worker thread.
  static auto reserve_thread() -> Count {
    lock_workers();

    auto worker_index =
        Algorithm::Search::find_min(View::Vector<U8>(thread_occupancy));
    if (thread_occupancy[worker_index] != 0) {
      unlock_workers();
      Diagnostics::Log::fatal(
          "Program requested to create a Process::Worker but all 64 Workers "
          "were already occupied."_view);
    }

    thread_occupancy[worker_index] = 1;
    unlock_workers();
    return worker_index;
  }

  static auto release_thread(Count worker_index) -> void {
    lock_workers();

    thread_occupancy[worker_index] = 0;
    unlock_workers();
  }

  static auto copy_thread_bytes(View::Bytes source_bytes) -> View::Bytes {
    // An empty view has stable representation and requires no reservation.
    if (source_bytes.is_empty()) {
      return View::Bytes();
    }

    const auto allocation =
        Allocator::Bibliotheca::check_out(source_bytes.get_size());
    Data::copy(
        allocation.ptr, source_bytes.get_data(), source_bytes.get_size());
    return View::Bytes(allocation.ptr, source_bytes.get_size());
  }

  static auto set_system_thread_name(View::Bytes thread_name) -> void {
    // An unnamed worker retains the platform supplied thread name.
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
    // Static::Bytes zero extends for names shorter than 16.
    Static::Bytes<16> system_thread_name(thread_name);

    // If the name is longer than 15 characters make sure we still have a null.
    system_thread_name[15] = 0;

    pthread_setname_np(
        pthread_self(), Data::cast<char>(system_thread_name.get_data()));

#endif
  }

  static auto dispatch(void* initializer_address) -> void* {
    // The spawning thread reserves this identity before native dispatch.
    auto& initializer = *Data::cast<ThreadInitializer>(initializer_address);
    this_thread_id = initializer.thread_id;

    // Set the thread name both in engine and at the OS level to aid in
    // debugging and tracing.
    this_thread_name = copy_thread_bytes(initializer.requested_name);
    Diagnostics::Log::set_thread_name(this_thread_name);
    set_system_thread_name(this_thread_name);

    // By default the worker inherits its spawning thread's diagnostic logging.
    // This allows for unified logging by default, but a job can later select
    // for alternative logging for subsequent messages.
    Diagnostics::Log::set_sink(initializer.sink);
    Diagnostics::Log::set_disable_header(initializer.disable_log_header);

    // Copy out the job data from the initializer before it's invalid.
    WorkerJobFunction job_function = initializer.job_function;
    View::Bytes job_data = copy_thread_bytes(initializer.job_data);

    // Log the thread start as a debug message using the inherited logger before
    // we hand off any execution to the job. This way the spawning thread will
    // always contain a spawn message when it successfully creates a thread.
    {
      Diagnostics::Log::Message<128> start_message(
          Diagnostics::Log::Level::Debug);
      start_message << "Process::Worker spawned worker_index="_view
                    << this_thread_id << " job_bytes="_view
                    << job_data.get_size();
    }

    // Run the thread job with the now thread localized job data.
    initializer.mark_initialized();
    job_function(job_data);

    // To improve observability the worker posts a completion message to the
    // spawning threads diagnostics to signal start / finished pairs. Since the
    // job might have changed the logger during execution we reset the sink to
    // the initalized logger.
    //
    // TODO: Two design considerations for the future:
    //
    // 1. We can also log a message to the worker's diagnostics but to avoid
    // repeat messages we would want to detect if that's safe somehow.
    //
    // 2. There is no guarantee that the spawning thread's logger hasn't
    // driffted since this worker was spawned. We might be reviving a stale
    // logger.
    {
      Diagnostics::Log::set_sink(initializer.sink);
      Diagnostics::Log::set_disable_header(initializer.disable_log_header);
      Diagnostics::Log::Message<128> exit_message(
          Diagnostics::Log::Level::Debug);
      exit_message << "Process::Worker completed worker_index="_view
                   << this_thread_id;
    }

    // The job data lives in this thread's Bibliotheca so thread and will be
    // closed on clean up so we need to mark the thread as free before closing
    // out.
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

  static inline Static::Vector<U8, Process::Worker::max_workers()>
      thread_occupancy;
};

Process::Worker::Worker(Worker&& other_worker) : handle(other_worker.handle) {
  other_worker.handle = 0;
}

Process::Worker::~Worker() {
  // Destruction joins the owned thread before releasing its native token.
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

    HANDLE thread = reinterpret_cast<HANDLE>(handle);
    const auto waited = WaitForSingleObject(thread, INFINITE);
    if (waited != WAIT_OBJECT_0) {
      Diagnostics::Log::fatal(
          "Process::Worker failed to join its thread."_view);
    }

    CloseHandle(thread);

#else

    pthread_t thread;
    Data::copy(
        reinterpret_cast<U8*>(&thread), reinterpret_cast<const U8*>(&handle),
        sizeof(thread));
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
