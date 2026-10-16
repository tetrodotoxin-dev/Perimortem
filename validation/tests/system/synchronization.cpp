// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/system/process/worker.hpp"
#include "perimortem/system/synchronization/mutex.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::System;
using namespace Toolchain::Validation;

static Harness SystemMutex = {
  .name = "System::Synchronization::Mutex",
};

static Synchronization::Mutex counter_mutex;
static Count counter = 0;
static constexpr Count worker_iterations = 10000;

static auto increment_counter(View::Bytes) -> void {
  for (Count iteration = 0; iteration < worker_iterations; iteration++) {
    Synchronization::Mutex::Lock lock(counter_mutex);
    counter++;
  }
}

static_assert(
    !__is_constructible(Synchronization::Mutex, const Synchronization::Mutex&));
static_assert(
    !__is_constructible(Synchronization::Mutex, Synchronization::Mutex&&));
static_assert(!__is_constructible(
    Synchronization::Mutex::Lock,
    const Synchronization::Mutex::Lock&));
static_assert(!__is_constructible(
    Synchronization::Mutex::Lock,
    Synchronization::Mutex::Lock&&));

VALIDATION_TEST(SystemMutex, serializes_workers) {
  constexpr Count worker_count = 4;
  counter = 0;

  Process::Worker workers[worker_count];
  for (Count index = 0; index < worker_count; index++) {
    workers[index] =
        Process::Worker::start("mutex.validation"_view, increment_counter);
  }

  for (Count index = 0; index < worker_count; index++) {
    workers[index].join();
  }

  EXPECT_EQ(counter, worker_count * worker_iterations);
}
