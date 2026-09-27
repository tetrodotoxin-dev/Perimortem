// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/null_terminated.hpp"

#include "perimortem/core/bibliotheca.hpp"

#include "toolchain/validation/process/child.hpp"
#include "tests/process_support/fixture.hpp"
#include "toolchain/validation/unit_test.hpp"

#include <signal.h>

#include "perimortem/core/static/vector.hpp"
#include "perimortem/core/algorithm/search.hpp"

using namespace Perimortem::Memory;
using namespace Perimortem::Core;
using namespace Toolchain::Validation;
using namespace Perimortem::Tests;

static Harness BibliothecaTests = {.name = "Core::Bibliotheca"};

// Oversized requests must reach the fatal diagnostic before either indexing
// the archive or shifting a size past Count's width. Child processes let us
// distinguish that deliberate abort from a memory fault without allocating
// a giant slab or terminating the test runner.
VALIDATION_TEST(BibliothecaTests, oversized_request) {
  const View::Bytes cases[] = {
    "allocator-limit"_view,  "allocator-overflow"_view, "vector-resize"_view,
    "vector-forgetful"_view, "arena-bytes"_view,        "arena-elements"_view,
  };
  const auto executable = fixture_path();
  ASSERT(!executable.is_empty());
  for (const auto mode : cases) {
    const Toolchain::Validation::Bytes arguments[] = {bytes(mode)};
    Process::Request request = {
      .executable = bytes(executable), .arguments = arguments, .argument_count = 1};
    auto child = Process::run(request);
    ASSERT(child.launched && !child.runner_error && !child.timed_out);
#ifdef PERI_WINDOWS
    EXPECT_EQ(child.exit_status, S32(3));
#else
    EXPECT_EQ(child.exit_status, S32(128 + SIGABRT));
#endif
    const auto diagnostic =
        mode == "arena-bytes"_view || mode == "arena-elements"_view
            ? "Arena allocation size overflow."_view
            : "Requested allocation exceeds Bibliotheca's size classes."_view;
    EXPECT(Algorithm::search(View::Bytes(static_cast<const U8*>(child.standard_error.data), child.standard_error.size), diagnostic) != Count(-1));
  }
}
