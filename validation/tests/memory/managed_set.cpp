// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/memory/managed/bytes.hpp"
#include "perimortem/memory/managed/set.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Toolchain::Validation;

static_assert(!__is_constructible(Managed::Set<U32>));
static_assert(__is_constructible(Managed::Set<U32>, Allocator::Arena&));

static Harness ManagedSet = {
  .name = "Managed::Set",
};

class ManagedSetValue {
 public:
  ManagedSetValue(Count value, Count& destructions)
      : value(value), destructions(&destructions) {}

  ManagedSetValue(const ManagedSetValue& source)
      : value(source.value), destructions(source.destructions) {}

  ~ManagedSetValue() { (*destructions)++; }

  constexpr auto hash() const -> U64 { return value; }
  constexpr auto operator==(const ManagedSetValue& rhs) const -> Bool {
    return value == rhs.value;
  }

  constexpr auto has_valid_location() const -> Bool { return location == this; }

 private:
  Count value;
  Count* destructions;
  const ManagedSetValue* const location = this;
};

VALIDATION_TEST(ManagedSet, value_semantics) {
  Allocator::Arena arena;
  Managed::Set<U32> values(arena);

  auto empty_copy = values;
  EXPECT_EQ(empty_copy.get_capacity(), Count(0));

  EXPECT(values.insert(4));
  EXPECT(!values.insert(4));
  EXPECT(values.insert(8));
  EXPECT(values.contains(4));
  EXPECT(values.remove(4));
  EXPECT(!values.contains(4));

  auto copied = values;
  copied.insert(16);
  EXPECT(!values.contains(16));
  EXPECT(copied.contains(16));
  EXPECT(&copied.get_arena() == &values.get_arena());
}

VALIDATION_TEST(ManagedSet, cross_arena_copy_assignment) {
  Allocator::Arena source_arena;
  Allocator::Arena destination_arena;
  Managed::Set<U32> source(source_arena);
  Managed::Set<U32> destination(destination_arena);
  source.insert(4);
  destination.insert(8);

  destination = source;

  EXPECT(&destination.get_arena() == &destination_arena);
  EXPECT(destination.contains(4));
  EXPECT(!destination.contains(8));
  source.insert(16);
  EXPECT(!destination.contains(16));
}

VALIDATION_TEST(ManagedSet, rvalue_assignment_copies) {
  Allocator::Arena source_arena;
  Allocator::Arena destination_arena;
  Managed::Set<U32> source(source_arena);
  Managed::Set<U32> destination(destination_arena);
  source.insert(4);

  destination = Data::take(source);

  EXPECT(&destination.get_arena() == &destination_arena);
  EXPECT(source.contains(4));
  EXPECT(destination.contains(4));
}

VALIDATION_TEST(ManagedSet, nested_values_use_destination_arena) {
  Allocator::Arena source_arena;
  Allocator::Arena destination_arena;
  Managed::Set<Managed::Bytes> source(source_arena);
  source.insert(Managed::Bytes(source_arena, "nested"_view));

  Managed::Set<Managed::Bytes> destination(destination_arena);
  destination = source;

  Count visited = 0;
  destination.visit([&](const Managed::Bytes& value) {
    visited++;
    EXPECT(&value.get_arena() == &destination_arena);
  });

  EXPECT_EQ(visited, Count(1));

  source_arena.reset();
  destination.visit([&](const Managed::Bytes& value) {
    EXPECT_EQ(value.get_view(), "nested"_view);
  });
}

VALIDATION_TEST(ManagedSet, nested_insert_uses_destination_arena) {
  Allocator::Arena source_arena;
  Allocator::Arena destination_arena;
  Managed::Bytes value(source_arena, "nested"_view);
  Managed::Set<Managed::Bytes> destination(destination_arena);

  EXPECT(destination.insert(value));
  source_arena.reset();

  destination.visit([&](const Managed::Bytes& stored_value) {
    EXPECT(&stored_value.get_arena() == &destination_arena);
    EXPECT_EQ(stored_value.get_view(), "nested"_view);
  });
}

VALIDATION_TEST(ManagedSet, clear_and_reset) {
  Allocator::Arena arena;
  Managed::Set<U32> values(arena, 32);
  const Count capacity = values.get_capacity();

  values.insert(4);

  values.clear();
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), capacity);

  values.reset();
  values.reset();
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), Count(0));
  EXPECT(&values.get_arena() == &arena);
}

VALIDATION_TEST(ManagedSet, empty_assignment_resets_capacity) {
  Allocator::Arena arena;
  Managed::Set<U32> empty(arena);
  Managed::Set<U32> values(arena, 32);
  values.insert(4);

  values = empty;

  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), Count(0));
  EXPECT(&values.get_arena() == &arena);
}

VALIDATION_TEST(ManagedSet, logical_capacity) {
  Allocator::Arena arena;
  Managed::Set<U32> values(arena);
  values.ensure_capacity(1000);

  const Count capacity = values.get_capacity();
  EXPECT(capacity >= 1000);
  values.ensure_capacity(capacity);
  EXPECT_EQ(values.get_capacity(), capacity);
}

VALIDATION_TEST(ManagedSet, growth_preserves_old_values) {
  Count destructions = 0;
  {
    Allocator::Arena arena;
    Managed::Set<ManagedSetValue> values(arena);
    ManagedSetValue first(1, destructions);
    values.insert(first);

    auto retained = values.find(first);
    ASSERT(retained);
    const ManagedSetValue* retained_location = &*retained;

    for (Count value = 2; value < 64; value++) {
      ManagedSetValue inserted(value, destructions);
      values.insert(inserted);
    }

    EXPECT(retained_location->has_valid_location());
    EXPECT(values.remove(first));
    values.clear();
    EXPECT_EQ(destructions, Count(62));
  }

  EXPECT_EQ(destructions, Count(63));
}
