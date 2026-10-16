// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/memory/global/bytes.hpp"
#include "perimortem/system/process/worker.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;
using namespace Toolchain::Validation;

static_assert(__is_constructible(Global::Bytes));
static_assert(__is_constructible(Global::Bytes, View::Bytes));
static_assert(
    sizeof(Global::Bytes) ==
    Data::align<alignof(Count)>(sizeof(U8*)) + sizeof(Count) * 2);
static_assert([] {
  Global::Bytes bytes;
  return bytes.is_empty() && bytes.get_capacity() == 0 &&
         bytes.get_view().get_data() == nullptr;
}());

static Harness GlobalBytes = {
  .name = "Global::Bytes",
};

static Global::Bytes transferred_bytes;

static auto create_global_bytes(View::Bytes source) -> void {
  Global::Bytes created(source);
  transferred_bytes = Data::take(created);
}

VALIDATION_TEST(GlobalBytes, value_semantics) {
  Global::Bytes original("global"_view);
  Global::Bytes copied(original);
  copied.append(U8('!'));

  EXPECT_EQ(original.get_view(), "global"_view);
  EXPECT_EQ(copied.get_view(), "global!"_view);
  EXPECT(copied.get_view().get_data() != original.get_view().get_data());
}

VALIDATION_TEST(GlobalBytes, cross_thread_transfer) {
  auto worker = Process::Worker::start(
      "global.bytes"_view, create_global_bytes, "worker owned"_view);
  worker.join();

  EXPECT_EQ(transferred_bytes.get_view(), "worker owned"_view);
  transferred_bytes.append(U8('!'));
  EXPECT_EQ(transferred_bytes.get_view(), "worker owned!"_view);

  // Main thread release proves that the process heap block can be released by a
  // thread other than the worker that allocated and populated the values.
  transferred_bytes.reset();
  EXPECT(!transferred_bytes.get_view().get_data());
}
