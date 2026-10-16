// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/view/bytes.h"

namespace Perimortem::System::Process {

// Worker represents one native thread and joins it before the handle leaves
// scope. Startup copies the name and job data into the destination thread
// before the spawning call returns, giving the job a stable thread local view.
class Worker {
 public:
  // The job function receives a stable copy of the provided job data for the
  // worker lifetime. The caller may release its original storage after start
  // returns.
  using JobFunction = void (*)(Core::View::Bytes);

  Worker() = default;
  EXPORTED(PERIMORTEM) ~Worker();
  EXPORTED(PERIMORTEM) Worker(Worker&&);
  EXPORTED(PERIMORTEM) auto operator=(Worker&&) -> Worker&;

  // Moving transfers the unique native thread token.
  Worker(const Worker&) = delete;
  auto operator=(const Worker&) -> Worker& = delete;

  // Explicitly blocks execution until the worker exits.
  // Automatically called on destruction.
  EXPORTED(PERIMORTEM) auto join() -> void;

  // Starts a worker on a separate thread with a copy of the provided job data.
  // Each worker receives a separate thread local Bibliotheca archive.
  static EXPORTED(PERIMORTEM) auto start(
      Core::View::Bytes name,
      JobFunction job_function,
      Core::View::Bytes job_data = Core::View::Bytes()) -> Worker;

  // Reports whether the current thread has the foreign identity. Workers use
  // their reserved slot as an identity while independently created native
  // threads begin with the all bits set identity.
  static EXPORTED(PERIMORTEM) auto on_main_thread() -> Bool;

  static EXPORTED(PERIMORTEM) auto get_thread_id() -> Count;
  static EXPORTED(PERIMORTEM) auto get_thread_name() -> Core::View::Bytes;

  // Returns the number of currently occupied worker slots.
  static EXPORTED(PERIMORTEM) auto get_worker_count() -> Count;

  static constexpr auto max_workers() -> Count { return 64; };

 private:
  // The implementation copies the native thread token into this storage. Its
  // size is checked there, without aliasing this integer as a pthread object.
  U64 handle = 0;
};

}  // namespace Perimortem::System::Process
