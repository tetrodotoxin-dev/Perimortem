// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/process/thread.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "validation/tests/log.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::System;
using namespace Perimortem::Tests;
using namespace Toolchain::Validation;

template <typename callback_type>
concept ValidThreadCallback = requires(callback_type& callback) {
  Process::Thread::start(View::Bytes(), callback);
};

static_assert(ValidThreadCallback<Process::Thread::Function>);
static_assert(!ValidThreadCallback<decltype(nullptr)>);

static Harness SystemProcessThread = {
  .name = "System::Process::Thread",
  .setup =
      []() {
        Log::begin();
        Diagnostics::Log::set_level(Diagnostics::Log::Level::Debug);
      },

  .teardown =
      []() {
        Diagnostics::Log::set_level(Diagnostics::Log::Level::Info);
        Log::end();
      },
};

struct ThreadResult {
  Count invocations = 0;
};

static auto record_invocation(void* context) -> void {
  auto& result = *Data::cast<ThreadResult>(context);
  result.invocations++;
}

using ThreadStart =
    Storage::Result<Process::Thread, Process::Thread::StartFailure>;

static_assert(__is_same(
    decltype(Process::Thread::start(View::Bytes(), record_invocation)),
    ThreadStart));

VALIDATION_TEST(SystemProcessThread, invokes_borrowed_context) {
  ThreadResult thread_result;
  auto started = Process::Thread::start(
      "context.validation"_view, record_invocation, &thread_result);
  started.visit(
      [](Process::Thread& thread) { thread.join(); },

      [&](Process::Thread::StartFailure) { EXPECT(false); });

  EXPECT_EQ(thread_result.invocations, Count(1));
  EXPECT_EQ(Log::captured_count(), Count(2));
  EXPECT_EQ(Log::captured_message(), "Process::Thread completed."_view);
}

VALIDATION_TEST(SystemProcessThread, destruction_joins) {
  ThreadResult thread_result;
  {
    auto started = Process::Thread::start(
        "destruction.validation"_view, record_invocation, &thread_result);
    started.visit(
        [](Process::Thread&) {},

        [&](Process::Thread::StartFailure) { EXPECT(false); });
  }

  EXPECT_EQ(thread_result.invocations, Count(1));
  EXPECT_EQ(Log::captured_count(), Count(2));
  EXPECT_EQ(Log::captured_message(), "Process::Thread completed."_view);
}

VALIDATION_TEST(SystemProcessThread, move_transfers_join) {
  ThreadResult thread_result;
  auto started = Process::Thread::start(
      "move.validation"_view, record_invocation, &thread_result);
  Process::Thread target;
  started.visit(
      [&](Process::Thread& source) {
        target = Data::take(source);
        source.join();
      },

      [&](Process::Thread::StartFailure) { EXPECT(false); });
  target.join();

  EXPECT_EQ(thread_result.invocations, Count(1));
  EXPECT_EQ(Log::captured_count(), Count(2));
  EXPECT_EQ(Log::captured_message(), "Process::Thread completed."_view);
}
