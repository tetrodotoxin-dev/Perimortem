// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/bibliotheca.hpp"
#include "perimortem/core/null_terminated.hpp"

#include "perimortem/memory/dynamic/bytes.hpp"

#include "toolchain/validation/unit_test.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Toolchain::Validation;

static Harness DynamicBytes = {
  .name = "Dynamic::Bytes",
};

VALIDATION_TEST(DynamicBytes, value_bounds) {
  Dynamic::Bytes bytes("abc"_view);

  EXPECT_EQ(bytes[2], U8('c'));
  EXPECT_EQ(bytes[3], U8(0));
  EXPECT_EQ(bytes.at(Count(-1)), U8(0));
  EXPECT_EQ(Dynamic::Bytes()[0], U8(0));
}

VALIDATION_TEST(DynamicBytes, copy) {
  Dynamic::Bytes original("copied"_view);
  Dynamic::Bytes copied(original);

  EXPECT(copied.get_view().get_data() != original.get_view().get_data());

  copied.append(U8('!'));

  EXPECT_EQ(original.get_view(), "copied"_view);
  EXPECT_EQ(copied.get_view(), "copied!"_view);

  Dynamic::Bytes assigned(128);
  const U8* allocation = assigned.get_view().get_data();
  assigned = original;
  EXPECT(assigned.get_view().get_data() == allocation);
  EXPECT(assigned.get_view().get_data() != original.get_view().get_data());
  EXPECT_EQ(assigned.get_view(), original.get_view());
  assigned.set(U8('x'));
  EXPECT_EQ(original.get_view(), "copied"_view);
}

VALIDATION_TEST(DynamicBytes, empty_copy) {
  Dynamic::Bytes reserved(128);
  Dynamic::Bytes copy(reserved);
  EXPECT(copy.is_empty());
  EXPECT_EQ(copy.get_capacity(), Count(0));
  EXPECT(!copy.get_view().get_data());

  copy.append(U8('a'));
  EXPECT_EQ(copy.get_view(), "a"_view);
  EXPECT(reserved.is_empty());
}

VALIDATION_TEST(DynamicBytes, borrowed_slices) {
  Dynamic::Bytes bytes("borrowed"_view);
  const U8* allocation = bytes.get_view().get_data();

  View::Bytes slice = bytes.slice(2, 4);

  EXPECT_EQ(slice, "rrow"_view);
  EXPECT(slice.get_data() == allocation + 2);
}

VALIDATION_TEST(DynamicBytes, reset) {
  Dynamic::Bytes bytes("released"_view);

  // Reset should be idempotent
  bytes.reset();
  bytes.reset();

  EXPECT(bytes.is_empty());
  EXPECT_EQ(bytes.get_capacity(), Count(0));
  EXPECT(!bytes.get_view().get_data());
}

VALIDATION_TEST(DynamicBytes, move_assignment) {
  Count initial = Bibliotheca::allocated_memory();
  {
    Dynamic::Bytes source("source"_view);
    Dynamic::Bytes target("target"_view);
    Count target_capacity = target.get_capacity();
    Count allocated = Bibliotheca::allocated_memory();
    const U8* source_allocation = source.get_view().get_data();

    target = static_cast<Dynamic::Bytes&&>(source);

    EXPECT_EQ(target.get_view(), "source"_view);
    EXPECT(target.get_view().get_data() == source_allocation);
    EXPECT(source.is_empty());
    EXPECT_EQ(source.get_capacity(), Count(0));
    EXPECT(!source.get_view().get_data());
    EXPECT_EQ(Bibliotheca::allocated_memory(), allocated - target_capacity);

    source.append(U8('r'));
    Dynamic::Bytes moved(static_cast<Dynamic::Bytes&&>(source));
    EXPECT_EQ(moved.get_view(), "r"_view);
    EXPECT_EQ(source.get_capacity(), Count(0));
    EXPECT(!source.get_view().get_data());
  }

  EXPECT_EQ(Bibliotheca::allocated_memory(), initial);
}

VALIDATION_TEST(DynamicBytes, self_assignment) {
  Dynamic::Bytes bytes("stable"_view);
  const Dynamic::Bytes& copied = bytes;
  bytes = copied;
  EXPECT_EQ(bytes.get_view(), "stable"_view);

  Dynamic::Bytes& moved = bytes;
  bytes = static_cast<Dynamic::Bytes&&>(moved);
  EXPECT_EQ(bytes.get_view(), "stable"_view);
}

VALIDATION_TEST(DynamicBytes, aliased_input) {
  Dynamic::Bytes bytes("abcdef"_view);
  bytes.proxy(bytes.slice(1, 4));
  EXPECT_EQ(bytes.get_view(), "bcde"_view);
  bytes.concat(bytes.get_view());
  EXPECT_EQ(bytes.get_view(), "bcdebcde"_view);

  bytes.resize(bytes.get_capacity());
  bytes.set(U8('x'));
  Count length = bytes.get_size();
  bytes.concat(bytes.get_view());
  EXPECT_EQ(bytes.get_size(), length * 2);
  for (Count index = 0; index < bytes.get_size(); index++) {
    EXPECT_EQ(bytes[index], U8('x'));
  }
}

VALIDATION_TEST(DynamicBytes, resize) {
  Dynamic::Bytes bytes("preserved"_view);
  Count capacity = bytes.get_capacity();
  bytes.resize(2);
  bytes.ensure_capacity(capacity + 1);
  bytes.resize(9);
  EXPECT_EQ(bytes.get_view(), "preserved"_view);
}

VALIDATION_TEST(DynamicBytes, forgetful_resize) {
  Dynamic::Bytes bytes(128);
  Count capacity = bytes.get_capacity();
  const U8* allocation = bytes.get_view().get_data();
  bytes.forgetful_resize(capacity - 1);
  EXPECT(bytes.get_view().get_data() == allocation);
  EXPECT_EQ(bytes.get_size(), capacity - 1);

  bytes.forgetful_resize(0);
  EXPECT(bytes.is_empty());
  EXPECT_EQ(bytes.get_capacity(), Count(0));
  EXPECT(!bytes.get_view().get_data());
}
