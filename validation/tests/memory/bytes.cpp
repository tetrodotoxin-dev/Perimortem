// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/global/bytes.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/allocator/bibliotheca.hpp"
#include "perimortem/memory/managed/bytes.hpp"
#include "perimortem/memory/thread/bytes.hpp"
#include "perimortem/system/process/worker.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;
using namespace Toolchain::Validation;

static_assert(__is_constructible(Thread::Bytes));
static_assert(__is_constructible(Thread::Bytes, View::Bytes));
static_assert(
    sizeof(Thread::Bytes) ==
    Data::align<alignof(Count)>(sizeof(U8*)) + sizeof(Count) * 2);
static_assert([] {
  Thread::Bytes bytes;
  return bytes.is_empty() && bytes.get_capacity() == 0 &&
         bytes.get_view().get_data() == nullptr;
}());

static Harness ThreadBytes = {
  .name = "Thread::Bytes",
};

// Indexing, copying, retained capacity, aliasing, reframing, and resizing
// belong to the Bytes capability. The factories supply each policy's
// construction requirements so every wrapper runs the same capability contract.
template <typename reserve_type, typename view_type>
static auto check_bytes_contract(
    reserve_type reserve,
    view_type from_view,
    Test::TestResult& result) -> void {
  auto empty = from_view(View::Bytes());
  EXPECT(empty.is_empty());
  EXPECT_EQ(empty.get_capacity(), Count(0));
  EXPECT(!empty.get_data());

  auto bytes = from_view("abc"_view);
  ASSERT(bytes[2]);
  bytes[2].visit([&](U8 value) { EXPECT_EQ(value, U8('c')); }, []() {});
  EXPECT(!bytes[3]);
  EXPECT(!bytes[Count(-1)]);

  auto copied = bytes;
  copied.append(U8('!'));
  EXPECT_EQ(bytes.get_view(), "abc"_view);
  EXPECT_EQ(copied.get_view(), "abc!"_view);
  EXPECT(copied.get_data() != bytes.get_data());

  auto reserved = reserve(128);
  const U8* reserved_allocation = reserved.get_data();
  reserved = bytes;
  EXPECT(reserved.get_data() == reserved_allocation);
  EXPECT_EQ(reserved.get_view(), bytes.get_view());

  auto empty_copy = reserve(128);
  auto empty_copy_result = empty_copy;
  EXPECT(empty_copy_result.is_empty());
  EXPECT_EQ(empty_copy_result.get_capacity(), Count(0));

  const U8* source_allocation = bytes.get_data();
  View::Bytes slice = bytes.slice(1, 2);
  EXPECT_EQ(slice, "bc"_view);
  EXPECT(slice.get_data() == source_allocation + 1);

  const auto& copied_self = bytes;
  bytes = copied_self;
  EXPECT_EQ(bytes.get_view(), "abc"_view);

  auto& moved_self = bytes;
  bytes = Data::take(moved_self);
  EXPECT_EQ(bytes.get_view(), "abc"_view);

  bytes = "abcdef"_view;
  bytes.copy(bytes.slice(1, 4));
  EXPECT_EQ(bytes.get_view(), "bcde"_view);
  bytes.concat(bytes.get_view());
  EXPECT_EQ(bytes.get_view(), "bcdebcde"_view);

  bytes = "abcdef"_view;

  const U8* reframe_allocation = bytes.get_data();
  bytes.reframe(2, 3);
  EXPECT_EQ(bytes.get_view(), "cde"_view);
  EXPECT(bytes.get_data() == reframe_allocation);

  bytes = "abc"_view;
  bytes.reframe(-2, 5);
  EXPECT_EQ(bytes.get_view(), "\0\0abc"_view);

  const Count reframe_capacity = bytes.get_capacity();
  bytes.reframe(0, 0);
  EXPECT(bytes.is_empty());
  EXPECT_EQ(bytes.get_capacity(), reframe_capacity);

  auto resized = reserve(128);
  resized = "preserved"_view;

  const Count resize_capacity = resized.get_capacity();
  resized.resize(2);
  resized.resize(9);
  EXPECT_EQ(resized.get_view(), "pr\0\0\0\0\0\0\0"_view);

  const U8* resize_allocation = resized.get_data();
  resized.resize(0);
  EXPECT(resized.is_empty());
  EXPECT_EQ(resized.get_capacity(), resize_capacity);
  EXPECT(resized.get_data() == resize_allocation);

  auto forgotten = reserve(128);
  const Count forgetful_capacity = forgotten.get_capacity();
  const U8* forgetful_allocation = forgotten.get_data();
  forgotten.forgetful_resize(forgetful_capacity - 1);
  EXPECT(forgotten.get_data() == forgetful_allocation);
  forgotten.forgetful_resize(forgetful_capacity >> 1);
  EXPECT(forgotten.get_capacity() < forgetful_capacity);
  forgotten.forgetful_resize(0);
  EXPECT(forgotten.is_empty());
  EXPECT_EQ(forgotten.get_capacity(), Count(0));
  EXPECT(!forgotten.get_data());

  bytes = "released"_view;
  bytes.reset();
  bytes.reset();
  EXPECT(bytes.is_empty());
  EXPECT_EQ(bytes.get_capacity(), Count(0));
  EXPECT(!bytes.get_data());
}

VALIDATION_TEST(ThreadBytes, capability_contract) {
  check_bytes_contract(
      [](Count capacity) { return Thread::Bytes(capacity); },
      [](View::Bytes values) { return Thread::Bytes(values); }, result);
}

VALIDATION_TEST(ThreadBytes, null_state) {
  const Count requests = Allocator::Bibliotheca::check_out_requests();
  Thread::Bytes bytes;

  EXPECT(bytes.is_empty());
  EXPECT_EQ(bytes.get_capacity(), Count(0));
  EXPECT(!bytes.get_view().get_data());
  EXPECT_EQ(Allocator::Bibliotheca::check_out_requests(), requests);
}

VALIDATION_TEST(ThreadBytes, empty_transition) {
  Thread::Bytes bytes("allocated"_view);
  const Count requests = Allocator::Bibliotheca::check_out_requests();

  bytes = View::Bytes();

  EXPECT(bytes.is_empty());
  EXPECT_EQ(Allocator::Bibliotheca::check_out_requests(), requests);
}

VALIDATION_TEST(ThreadBytes, move_assignment) {
  Count initial = Allocator::Bibliotheca::allocated_memory();
  {
    Thread::Bytes source("source"_view);
    Thread::Bytes target("target"_view);
    Count target_capacity = target.get_capacity();
    Count allocated = Allocator::Bibliotheca::allocated_memory();
    const U8* source_allocation = source.get_view().get_data();

    target = Data::take(source);

    EXPECT_EQ(target.get_view(), "source"_view);
    EXPECT(target.get_view().get_data() == source_allocation);
    EXPECT(source.is_empty());
    EXPECT_EQ(source.get_capacity(), Count(0));
    EXPECT(!source.get_view().get_data());
    EXPECT_EQ(
        Allocator::Bibliotheca::allocated_memory(),
        allocated - target_capacity);

    source.append(U8('r'));
    Thread::Bytes moved(Data::take(source));
    EXPECT_EQ(moved.get_view(), "r"_view);
    EXPECT_EQ(source.get_capacity(), Count(0));
    EXPECT(!source.get_view().get_data());
  }

  EXPECT_EQ(Allocator::Bibliotheca::allocated_memory(), initial);
}

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

VALIDATION_TEST(GlobalBytes, capability_contract) {
  check_bytes_contract(
      [](Count capacity) { return Global::Bytes(capacity); },
      [](View::Bytes values) { return Global::Bytes(values); }, result);
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

static_assert(!__is_constructible(Managed::Bytes));
static_assert(__is_constructible(Managed::Bytes, Allocator::Arena&));
static_assert(
    __is_constructible(Managed::Bytes, Allocator::Arena&, View::Bytes));

static Harness ManagedBytes = {
  .name = "Managed::Bytes",
};

VALIDATION_TEST(ManagedBytes, capability_contract) {
  Allocator::Arena arena;
  const Count requests = Allocator::Bibliotheca::check_out_requests();
  Managed::Bytes empty(arena, View::Bytes());
  EXPECT(empty.is_empty());
  EXPECT_EQ(Allocator::Bibliotheca::check_out_requests(), requests);

  check_bytes_contract(
      [&](Count capacity) { return Managed::Bytes(arena, capacity); },
      [&](View::Bytes values) { return Managed::Bytes(arena, values); },
      result);
}

VALIDATION_TEST(ManagedBytes, value_semantics) {
  Allocator::Arena source_arena;
  Allocator::Arena destination_arena;
  Managed::Bytes original(source_arena, "managed"_view);
  Managed::Bytes copied(original);
  copied.append(U8('!'));

  EXPECT_EQ(original.get_view(), "managed"_view);
  EXPECT_EQ(copied.get_view(), "managed!"_view);
  EXPECT(copied.get_view().get_data() != original.get_view().get_data());

  Managed::Bytes moved(Data::take(copied));
  EXPECT_EQ(moved.get_view(), "managed!"_view);
  EXPECT(copied.is_empty());
  EXPECT(!copied.get_view().get_data());

  Managed::Bytes assigned(destination_arena, "replaced"_view);
  assigned = original;
  EXPECT(&assigned.get_arena() == &destination_arena);
  EXPECT_EQ(assigned.get_view(), "managed"_view);

  Managed::Bytes transferred(destination_arena, "released"_view);
  transferred = Data::take(moved);
  EXPECT(&transferred.get_arena() == &destination_arena);
  EXPECT_EQ(transferred.get_view(), "managed!"_view);
  EXPECT_EQ(moved.get_view(), "managed!"_view);
  EXPECT(transferred.get_view().get_data() != moved.get_view().get_data());

  const auto retained = transferred.get_view();
  transferred.reset();
  EXPECT_EQ(retained, "managed!"_view);
}
