// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/mutex.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/null_terminated.hpp"
#include "perimortem/system/process/worker.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::System;
using namespace Toolchain::Validation;

static Harness SystemMutex = {
  .name = "System::Mutex",
};

static Mutex counter_mutex;
static Count counter = 0;
static constexpr Count worker_iterations = 10000;

static auto increment_counter(View::Bytes) -> void {
  for (Count iteration = 0; iteration < worker_iterations; iteration++) {
    Mutex::Lock lock(counter_mutex);
    counter++;
  }
}

static_assert(!__is_constructible(Mutex, const Mutex&));
static_assert(!__is_constructible(Mutex, Mutex&&));
static_assert(!__is_constructible(Mutex::Lock, const Mutex::Lock&));
static_assert(!__is_constructible(Mutex::Lock, Mutex::Lock&&));

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
