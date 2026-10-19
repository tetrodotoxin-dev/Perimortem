// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/global/bytes.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/allocator/bibliotheca.hpp"
#include "perimortem/memory/managed/bytes.hpp"
#include "perimortem/memory/thread/bytes.hpp"
#include "perimortem/system/process/thread.hpp"

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
  EXPECT_NOT(empty.try_access());
  EXPECT_EQ(empty.get_capacity(), Count(0));
  EXPECT(!empty.get_data());

  auto materialized_access = empty.get_access();
  EXPECT(empty.is_empty());
  EXPECT(empty.get_capacity() > 0);
  EXPECT(materialized_access.is_empty());
  EXPECT(materialized_access.get_data() != nullptr);
  EXPECT(materialized_access.get_data() == empty.get_data());

  auto retained_access = empty.try_access();
  ASSERT(retained_access);
  retained_access.visit(
      [&](Access::Bytes& access) {
        EXPECT(access.get_data() == empty.get_data());
        EXPECT(access.is_empty());
      },

      []() {});

  U8* retained_storage = empty.get_data();
  empty.clear();
  EXPECT(empty.get_data() == retained_storage);
  EXPECT(empty.try_access());
  empty.reset();
  EXPECT_NOT(empty.try_access());
  EXPECT_EQ(empty.get_capacity(), Count(0));

  auto preallocated = reserve(64);
  U8* preallocated_storage = preallocated.get_data();
  const Count preallocated_capacity = preallocated.get_capacity();
  auto preallocated_access = preallocated.get_access();
  EXPECT(preallocated.get_data() == preallocated_storage);
  EXPECT_EQ(preallocated.get_capacity(), preallocated_capacity);
  EXPECT(preallocated_access.get_data() == preallocated_storage);

  auto bytes = from_view("abc"_view);
  const auto& fixed_bytes = bytes;
  static_assert(__is_same(decltype(bytes[0]), Storage::Option<U8&>));
  static_assert(
      __is_same(decltype(fixed_bytes[0]), Storage::Option<const U8&>));
  ASSERT(bytes[2]);
  bytes[2].visit([&](U8 value) { EXPECT_EQ(value, U8('c')); }, []() {});
  fixed_bytes[1].visit([&](U8 value) { EXPECT_EQ(value, U8('b')); }, []() {});
  EXPECT(!bytes[3]);
  EXPECT(!bytes[Count(-1)]);

  bytes[1].visit([](U8& value) { value = U8('B'); }, []() {});
  EXPECT_EQ(bytes.get_view(), "aBc"_view);
  bytes.set(U8('x'));
  EXPECT_EQ(bytes.get_view(), "xxx"_view);
  EXPECT(bytes == "xxx"_view);
  EXPECT_EQ(bytes.hash(), Algorithm::Hash(bytes.get_view()).get_value());

  View::Bytes converted_view = bytes;
  Access::Bytes converted_access = bytes.get_access();
  EXPECT_EQ(converted_view, bytes.get_view());
  EXPECT_EQ(converted_access.get_size(), bytes.get_size());
  EXPECT(converted_access.get_data() == bytes.get_data());

  bytes = "abc"_view;
  bytes.append(U8('!'), 3);
  EXPECT_EQ(bytes.get_view(), "abc!!!"_view);
  bytes.append(U8('?'), 0);
  EXPECT_EQ(bytes.get_view(), "abc!!!"_view);

  bytes = "abc"_view;

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

  auto growing_concat = from_view("ab"_view);
  while (growing_concat.get_size() < growing_concat.get_capacity()) {
    growing_concat.append(U8('x'));
  }

  const Count concat_size = growing_concat.get_size();
  const U8* concat_allocation = growing_concat.get_data();
  growing_concat.concat(growing_concat.slice(0, 2));
  EXPECT_EQ(growing_concat.get_size(), concat_size + 2);
  EXPECT_EQ(growing_concat.get_data()[concat_size], U8('a'));
  EXPECT_EQ(growing_concat.get_data()[concat_size + 1], U8('b'));
  EXPECT(growing_concat.get_data() != concat_allocation);

  // Each destination index maps to the source index at `index + start`.
  // Exhausting the nearby start and size combinations verifies clipping,
  // overlap, zero padding and allocation reuse without copying implementation
  // branches into the test.
  constexpr auto reframe_source = "abcdef"_view;
  for (S64 start = -8; start <= 8; start++) {
    for (Count requested_size = 0; requested_size <= 12; requested_size++) {
      auto reframed = from_view(reframe_source);
      const Count previous_capacity = reframed.get_capacity();
      const U8* previous_allocation = reframed.get_data();

      reframed.reframe(start, requested_size);

      EXPECT_EQ(reframed.get_size(), requested_size);
      for (Count index = 0; index < requested_size; index++) {
        const S64 source_index = S64(index) + start;
        const U8 expected =
            source_index >= 0 && Count(source_index) < reframe_source.get_size()
                ? reframe_source[Count(source_index)]
                : U8(0);
        EXPECT_EQ(reframed.get_data()[index], expected);
      }

      if (requested_size <= previous_capacity) {
        EXPECT(reframed.get_data() == previous_allocation);
        EXPECT_EQ(reframed.get_capacity(), previous_capacity);
      } else {
        EXPECT(reframed.get_data() != previous_allocation);
        EXPECT(reframed.get_capacity() >= requested_size);
      }
    }
  }

  auto growing_reframe = reserve(8);
  growing_reframe = "abc"_view;

  const Count requested_size = growing_reframe.get_capacity() + 2;
  const U8* growing_reframe_allocation = growing_reframe.get_data();
  growing_reframe.reframe(-1, requested_size);
  EXPECT(growing_reframe.get_data() != growing_reframe_allocation);
  EXPECT_EQ(growing_reframe.get_size(), requested_size);
  EXPECT_EQ(growing_reframe.get_data()[0], U8(0));
  EXPECT_EQ(growing_reframe.get_data()[1], U8('a'));
  EXPECT_EQ(growing_reframe.get_data()[2], U8('b'));
  EXPECT_EQ(growing_reframe.get_data()[3], U8('c'));
  for (Count index = 4; index < growing_reframe.get_size(); index++) {
    EXPECT_EQ(growing_reframe.get_data()[index], U8(0));
  }

  auto ensured = from_view("capacity"_view);
  const Count ensured_capacity = ensured.get_capacity();
  const U8* ensured_allocation = ensured.get_data();
  ensured.ensure_capacity(ensured_capacity);
  EXPECT_EQ(ensured.get_capacity(), ensured_capacity);
  EXPECT(ensured.get_data() == ensured_allocation);
  ensured.ensure_capacity(ensured_capacity + 1);
  EXPECT(ensured.get_capacity() > ensured_capacity);
  EXPECT_EQ(ensured.get_view(), "capacity"_view);
  EXPECT(ensured.get_data() != ensured_allocation);

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

  auto growing_resize = from_view("abc"_view);
  const Count growing_resize_capacity = growing_resize.get_capacity();
  growing_resize.resize(growing_resize_capacity + 2);
  EXPECT(growing_resize.get_capacity() > growing_resize_capacity);
  EXPECT_EQ(growing_resize.get_data()[0], U8('a'));
  EXPECT_EQ(growing_resize.get_data()[1], U8('b'));
  EXPECT_EQ(growing_resize.get_data()[2], U8('c'));
  for (Count index = 3; index < growing_resize.get_size(); index++) {
    EXPECT_EQ(growing_resize.get_data()[index], U8(0));
  }

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

  auto cleared = from_view("retained"_view);
  const Count cleared_capacity = cleared.get_capacity();
  const U8* cleared_allocation = cleared.get_data();
  cleared.clear();
  EXPECT(cleared.is_empty());
  EXPECT_EQ(cleared.get_capacity(), cleared_capacity);
  EXPECT(cleared.get_data() == cleared_allocation);
  cleared.append(U8('r'));
  EXPECT_EQ(cleared.get_view(), "r"_view);

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

static auto create_global_bytes(void*) -> void {
  Global::Bytes created("thread owned"_view);
  transferred_bytes = Data::take(created);
}

VALIDATION_TEST(GlobalBytes, capability_contract) {
  check_bytes_contract(
      [](Count capacity) { return Global::Bytes(capacity); },
      [](View::Bytes values) { return Global::Bytes(values); }, result);
}

VALIDATION_TEST(GlobalBytes, cross_thread_transfer) {
  auto started =
      Process::Thread::start("global.bytes"_view, create_global_bytes);
  started.visit(
      [](Process::Thread& thread) { thread.join(); },

      [&](Process::Thread::StartFailure) { EXPECT(false); });

  EXPECT_EQ(transferred_bytes.get_view(), "thread owned"_view);
  transferred_bytes.append(U8('!'));
  EXPECT_EQ(transferred_bytes.get_view(), "thread owned!"_view);

  // Main thread release proves that the process heap block can be released by a
  // thread other than the one that allocated and populated the values.
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

  EXPECT(&copied.get_arena() == &source_arena);
  EXPECT_EQ(original.get_view(), "managed"_view);
  EXPECT_EQ(copied.get_view(), "managed!"_view);
  EXPECT(copied.get_view().get_data() != original.get_view().get_data());

  const U8* moved_allocation = copied.get_data();
  Managed::Bytes moved(Data::take(copied));
  EXPECT(&moved.get_arena() == &source_arena);
  EXPECT(moved.get_data() == moved_allocation);
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
