// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/memory/allocator/bibliotheca.hpp"
#include "perimortem/memory/thread/bytes.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
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

VALIDATION_TEST(ThreadBytes, value_bounds) {
  Thread::Bytes bytes("abc"_view);

  auto selected = bytes[2];
  ASSERT(selected);
  EXPECT_EQ(*selected, U8('c'));
  EXPECT(!bytes[3]);
  EXPECT(!bytes[Count(-1)]);
  EXPECT(!Thread::Bytes()[0]);
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

VALIDATION_TEST(ThreadBytes, copy) {
  Thread::Bytes original("copied"_view);
  Thread::Bytes copied(original);

  EXPECT(copied.get_view().get_data() != original.get_view().get_data());

  copied.append(U8('!'));

  EXPECT_EQ(original.get_view(), "copied"_view);
  EXPECT_EQ(copied.get_view(), "copied!"_view);

  Thread::Bytes assigned(128);
  const U8* allocation = assigned.get_view().get_data();
  assigned = original;
  EXPECT(assigned.get_view().get_data() == allocation);
  EXPECT(assigned.get_view().get_data() != original.get_view().get_data());
  EXPECT_EQ(assigned.get_view(), original.get_view());
  assigned.set(U8('x'));
  EXPECT_EQ(original.get_view(), "copied"_view);
}

VALIDATION_TEST(ThreadBytes, empty_copy) {
  Thread::Bytes reserved(128);
  Thread::Bytes copy(reserved);
  EXPECT(copy.is_empty());
  EXPECT_EQ(copy.get_capacity(), Count(0));
  EXPECT(!copy.get_view().get_data());

  copy.append(U8('a'));
  EXPECT_EQ(copy.get_view(), "a"_view);
  EXPECT(reserved.is_empty());
}

VALIDATION_TEST(ThreadBytes, view_slice) {
  Thread::Bytes bytes("source"_view);
  const U8* allocation = bytes.get_view().get_data();

  View::Bytes slice = bytes.slice(1, 4);

  EXPECT_EQ(slice, "ourc"_view);
  EXPECT(slice.get_data() == allocation + 1);
}

VALIDATION_TEST(ThreadBytes, reset) {
  Thread::Bytes bytes("released"_view);

  bytes.reset();
  bytes.reset();

  EXPECT(bytes.is_empty());
  EXPECT_EQ(bytes.get_capacity(), Count(0));
  EXPECT(!bytes.get_view().get_data());
}

VALIDATION_TEST(ThreadBytes, move_assignment) {
  Count initial = Allocator::Bibliotheca::allocated_memory();
  {
    Thread::Bytes source("source"_view);
    Thread::Bytes target("target"_view);
    Count target_capacity = target.get_capacity();
    Count allocated = Allocator::Bibliotheca::allocated_memory();
    const U8* source_allocation = source.get_view().get_data();

    target = static_cast<Thread::Bytes&&>(source);

    EXPECT_EQ(target.get_view(), "source"_view);
    EXPECT(target.get_view().get_data() == source_allocation);
    EXPECT(source.is_empty());
    EXPECT_EQ(source.get_capacity(), Count(0));
    EXPECT(!source.get_view().get_data());
    EXPECT_EQ(
        Allocator::Bibliotheca::allocated_memory(),
        allocated - target_capacity);

    source.append(U8('r'));
    Thread::Bytes moved(static_cast<Thread::Bytes&&>(source));
    EXPECT_EQ(moved.get_view(), "r"_view);
    EXPECT_EQ(source.get_capacity(), Count(0));
    EXPECT(!source.get_view().get_data());
  }

  EXPECT_EQ(Allocator::Bibliotheca::allocated_memory(), initial);
}

VALIDATION_TEST(ThreadBytes, self_assignment) {
  Thread::Bytes bytes("stable"_view);
  const Thread::Bytes& copied = bytes;
  bytes = copied;
  EXPECT_EQ(bytes.get_view(), "stable"_view);

  Thread::Bytes& moved = bytes;
  bytes = static_cast<Thread::Bytes&&>(moved);
  EXPECT_EQ(bytes.get_view(), "stable"_view);
}

VALIDATION_TEST(ThreadBytes, aliased_input) {
  Thread::Bytes bytes("abcdef"_view);
  bytes.copy(bytes.slice(1, 4));
  EXPECT_EQ(bytes.get_view(), "bcde"_view);
  bytes.concat(bytes.get_view());
  EXPECT_EQ(bytes.get_view(), "bcdebcde"_view);

  bytes.resize(bytes.get_capacity());
  bytes.set(U8('x'));

  Count length = bytes.get_size();
  bytes.concat(bytes.get_view());
  EXPECT_EQ(bytes.get_size(), length * 2);
  for (Count index = 0; index < bytes.get_size(); index++) {
    EXPECT_EQ(bytes.get_data()[index], U8('x'));
  }
}

VALIDATION_TEST(ThreadBytes, reframe_subrange) {
  Thread::Bytes bytes("abcdef"_view);
  const U8* allocation = bytes.get_view().get_data();

  bytes.reframe(2, 3);

  EXPECT_EQ(bytes.get_view(), "cde"_view);
  EXPECT(bytes.get_view().get_data() == allocation);

  bytes = "abcdef"_view;
  bytes.reframe(4, 5);
  EXPECT_EQ(bytes.get_view(), "ef\0\0\0"_view);
}

VALIDATION_TEST(ThreadBytes, reframe_padding) {
  Thread::Bytes bytes("abcdef"_view);
  bytes.reframe(-2, 8);

  EXPECT_EQ(bytes.get_view(), "\0\0abcdef"_view);

  bytes = "abc"_view;

  const Count expanded_size = bytes.get_capacity() + 1;
  bytes.reframe(-2, expanded_size);
  EXPECT_EQ(bytes.slice(0, 5), "\0\0abc"_view);
  EXPECT_EQ(bytes.get_size(), expanded_size);
  EXPECT_EQ(bytes.get_data()[expanded_size - 1], U8(0));

  constexpr S64 minimum = -9223372036854775807LL - 1;
  bytes.reframe(minimum, 4);
  EXPECT_EQ(bytes.get_view(), "\0\0\0\0"_view);

  bytes = "abc"_view;
  bytes.reframe(20, 4);
  EXPECT_EQ(bytes.get_view(), "\0\0\0\0"_view);

  const Count capacity = bytes.get_capacity();
  bytes.reframe(0, 0);
  EXPECT(bytes.is_empty());
  EXPECT_EQ(bytes.get_capacity(), capacity);
}

VALIDATION_TEST(ThreadBytes, resize) {
  Thread::Bytes bytes(128);
  bytes = "preserved"_view;

  const Count capacity = bytes.get_capacity();
  bytes.resize(2);
  bytes.resize(9);
  EXPECT_EQ(bytes.get_view(), "pr\0\0\0\0\0\0\0"_view);

  bytes = "preserved"_view;
  bytes.resize(2);
  bytes.ensure_capacity(capacity + 1);
  bytes.resize(9);
  EXPECT_EQ(bytes.get_view(), "pr\0\0\0\0\0\0\0"_view);

  const U8* allocation = bytes.get_data();
  const Count retained_capacity = bytes.get_capacity();
  bytes.resize(0);
  EXPECT(bytes.is_empty());
  EXPECT_EQ(bytes.get_capacity(), retained_capacity);
  EXPECT(bytes.get_data() == allocation);
}

VALIDATION_TEST(ThreadBytes, forgetful_resize) {
  Thread::Bytes bytes(128);
  Count capacity = bytes.get_capacity();
  const U8* allocation = bytes.get_view().get_data();
  bytes.forgetful_resize(capacity - 1);
  EXPECT(bytes.get_view().get_data() == allocation);
  EXPECT_EQ(bytes.get_size(), capacity - 1);

  bytes.forgetful_resize(capacity >> 1);
  EXPECT_EQ(bytes.get_size(), capacity >> 1);
  EXPECT(bytes.get_capacity() < capacity);

  bytes.forgetful_resize(0);
  EXPECT(bytes.is_empty());
  EXPECT_EQ(bytes.get_capacity(), Count(0));
  EXPECT(!bytes.get_view().get_data());
}
