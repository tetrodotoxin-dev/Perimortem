// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/process/thread.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/view/null_terminated.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::System;
using namespace Toolchain::Validation;

static Harness SystemProcessThread = {
  .name = "System::Process::Thread",
};

struct ThreadResult {
  Count invocations = 0;
};

static auto record_invocation(void* context) -> void {
  auto& result = *Data::cast<ThreadResult>(context);
  result.invocations++;
}

VALIDATION_TEST(SystemProcessThread, invokes_borrowed_context) {
  ThreadResult thread_result;
  Process::Thread thread = Process::Thread::start(
      "context.validation"_view, record_invocation, &thread_result);
  thread.join();

  EXPECT_EQ(thread_result.invocations, Count(1));
}

VALIDATION_TEST(SystemProcessThread, destruction_joins) {
  ThreadResult thread_result;
  {
    Process::Thread thread = Process::Thread::start(
        "destruction.validation"_view, record_invocation, &thread_result);
  }

  EXPECT_EQ(thread_result.invocations, Count(1));
}

VALIDATION_TEST(SystemProcessThread, move_transfers_join) {
  ThreadResult thread_result;
  Process::Thread source = Process::Thread::start(
      "move.validation"_view, record_invocation, &thread_result);
  Process::Thread target(Data::take(source));
  source.join();
  target.join();

  EXPECT_EQ(thread_result.invocations, Count(1));
}
