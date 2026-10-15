// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/core/null_terminated.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/allocator/bibliotheca.hpp"
#include "perimortem/memory/managed/bytes.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Toolchain::Validation;

static_assert(!__is_constructible(Managed::Bytes));
static_assert(__is_constructible(Managed::Bytes, Allocator::Arena&));
static_assert(
    __is_constructible(Managed::Bytes, Allocator::Arena&, View::Bytes));

static Harness ManagedBytes = {
  .name = "Managed::Bytes",
};

VALIDATION_TEST(ManagedBytes, empty_state) {
  Allocator::Arena arena;
  const Count requests = Allocator::Bibliotheca::check_out_requests();
  Managed::Bytes bytes(arena, View::Bytes());

  EXPECT(bytes.is_empty());
  EXPECT_EQ(bytes.get_capacity(), Count(0));
  EXPECT(!bytes.get_view().get_data());
  EXPECT_EQ(Allocator::Bibliotheca::check_out_requests(), requests);
}

VALIDATION_TEST(ManagedBytes, bounds) {
  Allocator::Arena arena;
  Managed::Bytes bytes(arena, "abc"_view);

  auto selected = bytes[2];
  ASSERT(selected);
  EXPECT_EQ(*selected, U8('c'));
  EXPECT(!bytes[3]);
  EXPECT(!bytes[Count(-1)]);
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

  Managed::Bytes moved(static_cast<Managed::Bytes&&>(copied));
  EXPECT_EQ(moved.get_view(), "managed!"_view);
  EXPECT(copied.is_empty());
  EXPECT(!copied.get_view().get_data());

  Managed::Bytes assigned(destination_arena, "replaced"_view);
  assigned = original;
  EXPECT(&assigned.get_arena() == &destination_arena);
  EXPECT_EQ(assigned.get_view(), "managed"_view);

  Managed::Bytes transferred(destination_arena, "released"_view);
  transferred = static_cast<Managed::Bytes&&>(moved);
  EXPECT(&transferred.get_arena() == &destination_arena);
  EXPECT_EQ(transferred.get_view(), "managed!"_view);
  EXPECT_EQ(moved.get_view(), "managed!"_view);
  EXPECT(transferred.get_view().get_data() != moved.get_view().get_data());

  const auto retained = transferred.get_view();
  transferred.reset();
  EXPECT_EQ(retained, "managed!"_view);
}
