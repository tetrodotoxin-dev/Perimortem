// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/process/library.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "validation/tests/log.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;
using namespace Perimortem::Tests;
using namespace Toolchain::Validation;

#ifdef TOOLCHAIN_WINDOWS

static constexpr View::Bytes host_library = "kernel32.dll"_view;
static constexpr View::Bytes host_symbol = "GetCurrentProcessId"_view;

#elifdef TOOLCHAIN_LINUX

static constexpr View::Bytes host_library = "libc.so.6"_view;
static constexpr View::Bytes host_symbol = "malloc"_view;

#endif

static Harness SystemProcessLibrary = {
  .name = "System::Process::Library",
  .setup = Log::begin,
  .teardown = Log::end,
};

VALIDATION_TEST(SystemProcessLibrary, missing_library) {
  Allocator::Arena errors;
  auto opened = Process::Library::open(
      "perimortem_validation_missing_library"_view, errors);
  opened.visit(
      [&](Process::Library&) { EXPECT(false); },

      [&](View::Bytes message) { EXPECT_NOT(message.is_empty()); });

  EXPECT_EQ(Log::captured_count(), Count(0));
}

VALIDATION_TEST(SystemProcessLibrary, symbol_and_move_lifetime) {

#if defined(TOOLCHAIN_LINUX) || defined(TOOLCHAIN_WINDOWS)

  Allocator::Arena errors;
  auto opened = Process::Library::open(host_library, errors);
  opened.visit(
      [&](Process::Library& library) {
        Process::Library moved(Data::take(library));

        auto symbol = moved.symbol(host_symbol, errors);
        symbol.visit(
            [&](void* address) { EXPECT(address); },

            [&](View::Bytes) { EXPECT(false); });

        auto missing =
            moved.symbol("perimortem_validation_missing_symbol"_view, errors);
        missing.visit(
            [&](void*) { EXPECT(false); },

            [&](View::Bytes message) { EXPECT_NOT(message.is_empty()); });
      },

      [&](View::Bytes) { EXPECT(false); });

  EXPECT_EQ(Log::captured_count(), Count(0));

#else

  SKIP("The host does not provide a native dynamic module fixture."_view);

#endif
}
