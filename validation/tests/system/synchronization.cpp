// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/system/process/thread.hpp"
#include "perimortem/system/synchronization/mutex.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::System;
using namespace Toolchain::Validation;

static Harness SystemMutex = {
  .name = "System::Synchronization::Mutex",
};

static Synchronization::Mutex counter_mutex;
static Count counter = 0;
static constexpr Count thread_iterations = 10000;

static auto increment_counter(void*) -> void {
  for (Count iteration = 0; iteration < thread_iterations; iteration++) {
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

VALIDATION_TEST(SystemMutex, serializes_threads) {
  constexpr Count thread_count = 4;
  counter = 0;

  Process::Thread threads[thread_count];
  for (Count index = 0; index < thread_count; index++) {
    threads[index] =
        Process::Thread::start("mutex.validation"_view, increment_counter);
  }

  for (Count index = 0; index < thread_count; index++) {
    threads[index].join();
  }

  EXPECT_EQ(counter, thread_count * thread_iterations);
}
