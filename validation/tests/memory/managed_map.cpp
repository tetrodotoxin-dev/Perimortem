// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/managed/bytes.hpp"
#include "perimortem/memory/managed/map.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Toolchain::Validation;

static_assert(!__is_constructible(Managed::Map<U32, U32>));
static_assert(__is_constructible(Managed::Map<U32, U32>, Allocator::Arena&));

static Harness ManagedMap = {
  .name = "Managed::Map",
};

class ManagedMapValue {
 public:
  ManagedMapValue(Count value, Count& destructions)
      : value(value), destructions(&destructions) {}

  ManagedMapValue(const ManagedMapValue& source)
      : value(source.value), destructions(source.destructions) {}

  ~ManagedMapValue() { (*destructions)++; }

  constexpr auto get_value() const -> Count { return value; }
  constexpr auto has_valid_location() const -> Bool { return location == this; }

 private:
  Count value;
  Count* destructions;
  const ManagedMapValue* const location = this;
};

VALIDATION_TEST(ManagedMap, empty) {
  Allocator::Arena arena;
  Managed::Map<S32, S32> values(arena);

  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_size(), Count(0));
  EXPECT(!values.find(4));
}

VALIDATION_TEST(ManagedMap, find) {
  Allocator::Arena arena;
  Managed::Map<S32, S32> values(arena);
  static_assert(__is_same(decltype(values.find(4)), Option<S32&>));

  values.insert(4, 5);

  auto found = values.find(4);
  ASSERT(found);
  EXPECT_EQ(*found, 5);
  EXPECT(!values.find(8));

  const auto& const_values = values;
  static_assert(__is_same(decltype(const_values.find(4)), Option<const S32&>));
  auto const_found = const_values.find(4);
  ASSERT(const_found);
  EXPECT_EQ(*const_found, 5);
  EXPECT(!const_values.find(8));
}

VALIDATION_TEST(ManagedMap, simple_insert) {
  Allocator::Arena arena;
  Managed::Map<S32, S32> values(arena);

  values.insert(1, 2);
  values.insert(2, 3);
  values.insert(4, 5);

  EXPECT_EQ(values.get_size(), Count(3));
  EXPECT_EQ(*values[1], 2);
  EXPECT_EQ(*values[2], 3);
  EXPECT_EQ(*values[4], 5);
}

VALIDATION_TEST(ManagedMap, duplicate_keys) {
  Allocator::Arena arena;
  Managed::Map<S32, S32> values(arena);

  EXPECT(values.insert(1, 2));
  EXPECT(!values.insert(1, 4));
  values.insert(2, 8);

  EXPECT_EQ(values.get_size(), Count(2));
  EXPECT_EQ(*values[1], 2);

  EXPECT_EQ(*values.assign(1, 4), 4);
  EXPECT_EQ(*values[1], 4);
  EXPECT_EQ(*values[2], 8);
}

VALIDATION_TEST(ManagedMap, visit) {
  Allocator::Arena arena;
  Managed::Map<S32, S32> values(arena);

  values.insert(1, 2);

  S32 key_sum = 0;
  S32 value_sum = 0;
  values.visit([&](const S32& key, S32& value) {
    key_sum += key;
    value_sum += value;
    value++;
  });

  EXPECT_EQ(key_sum, 1);
  EXPECT_EQ(value_sum, 2);
  EXPECT_EQ(*values[1], 3);
}

VALIDATION_TEST(ManagedMap, text_keys) {
  Allocator::Arena arena;
  Managed::Map<View::Bytes, S32> values(arena);

  values.insert("Hello"_view, 1);
  values.insert("World"_view, 2);
  values.insert("Longer test string"_view, 3);

  EXPECT(values.contains("Hello"_view));
  EXPECT_EQ(*values["Hello"_view], 1);
  EXPECT_EQ(*values["World"_view], 2);
  EXPECT_EQ(*values["Longer test string"_view], 3);
}

VALIDATION_TEST(ManagedMap, clear) {
  Allocator::Arena arena;
  Managed::Map<S32, S32> values(arena);

  values.insert(1, 2);
  values.insert(2, 3);
  values.clear();

  EXPECT(values.is_empty());
  EXPECT(!values.contains(1));
  values.insert(1, 4);
  EXPECT_EQ(*values[1], 4);
}

VALIDATION_TEST(ManagedMap, independent_copy) {
  Allocator::Arena arena;
  Managed::Map<S32, S32> source(arena);
  source.insert(1, 2);
  source.insert(3, 4);

  auto copy = source;
  copy.assign(1, 9);
  copy.insert(5, 6);
  EXPECT_EQ(source.get_size(), Count(2));
  EXPECT_EQ(*source[1], 2);
  EXPECT(!source.contains(5));

  copy.clear();
  EXPECT(copy.is_empty());
  EXPECT_EQ(source.get_size(), Count(2));
  EXPECT(source.contains(1));
  EXPECT(source.contains(3));
}

VALIDATION_TEST(ManagedMap, cross_arena_copy_assignment) {
  Allocator::Arena source_arena;
  Allocator::Arena destination_arena;
  Managed::Map<S32, S32> source(source_arena);
  Managed::Map<S32, S32> destination(destination_arena);
  source.insert(1, 2);
  destination.insert(3, 4);

  destination = source;

  EXPECT(&destination.get_arena() == &destination_arena);
  EXPECT_EQ(destination.get_size(), Count(1));
  EXPECT_EQ(*destination[1], 2);
  *source[1] = 8;
  EXPECT_EQ(*destination[1], 2);
}

VALIDATION_TEST(ManagedMap, rvalue_assignment_copies) {
  Allocator::Arena source_arena;
  Allocator::Arena destination_arena;
  Managed::Map<S32, S32> source(source_arena);
  Managed::Map<S32, S32> destination(destination_arena);
  source.insert(1, 2);

  destination = Data::take(source);

  EXPECT(&destination.get_arena() == &destination_arena);
  EXPECT(source.contains(1));
  EXPECT(destination.contains(1));
}

VALIDATION_TEST(ManagedMap, nested_values_use_destination_arena) {
  Allocator::Arena source_arena;
  Allocator::Arena destination_arena;
  Managed::Map<Managed::Bytes, Managed::Bytes> source(source_arena);
  source.insert(
      Managed::Bytes(source_arena, "key"_view),
      Managed::Bytes(source_arena, "value"_view));

  Managed::Map<Managed::Bytes, Managed::Bytes> destination(destination_arena);
  destination = source;

  Count visited = 0;
  destination.visit([&](const Managed::Bytes& key, Managed::Bytes& value) {
    visited++;
    EXPECT(&key.get_arena() == &destination_arena);
    EXPECT(&value.get_arena() == &destination_arena);
  });

  EXPECT_EQ(visited, Count(1));

  source_arena.reset();
  destination.visit([&](const Managed::Bytes& key, Managed::Bytes& value) {
    EXPECT_EQ(key.get_view(), "key"_view);
    EXPECT_EQ(value.get_view(), "value"_view);
  });
}

VALIDATION_TEST(ManagedMap, nested_insert_uses_destination_arena) {
  Allocator::Arena source_arena;
  Allocator::Arena destination_arena;
  Managed::Bytes key(source_arena, "key"_view);
  Managed::Bytes value(source_arena, "value"_view);
  Managed::Map<Managed::Bytes, Managed::Bytes> destination(destination_arena);

  EXPECT(destination.insert(key, value));
  source_arena.reset();

  destination.visit(
      [&](const Managed::Bytes& stored_key, Managed::Bytes& stored_value) {
        EXPECT(&stored_key.get_arena() == &destination_arena);
        EXPECT(&stored_value.get_arena() == &destination_arena);
        EXPECT_EQ(stored_key.get_view(), "key"_view);
        EXPECT_EQ(stored_value.get_view(), "value"_view);
      });
}

VALIDATION_TEST(ManagedMap, clear_and_reset) {
  Allocator::Arena arena;
  Managed::Map<S32, S32> values(arena, 32);
  const Count capacity = values.get_capacity();

  values.insert(1, 2);

  values.clear();
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), capacity);

  values.reset();
  values.reset();
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), Count(0));
  EXPECT(&values.get_arena() == &arena);
}

VALIDATION_TEST(ManagedMap, empty_copy) {
  Allocator::Arena arena;
  Managed::Map<S32, S32> source(arena);
  auto copy = source;
  EXPECT_EQ(copy.get_capacity(), Count(0));
  copy.insert(1, 2);

  EXPECT(source.is_empty());
  EXPECT_EQ(copy.get_size(), Count(1));
}

VALIDATION_TEST(ManagedMap, empty_assignment_resets_capacity) {
  Allocator::Arena arena;
  Managed::Map<S32, S32> empty(arena);
  Managed::Map<S32, S32> values(arena, 32);
  values.insert(1, 2);

  values = empty;

  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), Count(0));
  EXPECT(&values.get_arena() == &arena);
}

VALIDATION_TEST(ManagedMap, logical_capacity) {
  Allocator::Arena arena;
  Managed::Map<S32, S32> values(arena);
  values.ensure_capacity(1000);

  const Count capacity = values.get_capacity();
  EXPECT(capacity >= 1000);
  values.ensure_capacity(capacity);
  EXPECT_EQ(values.get_capacity(), capacity);
}

VALIDATION_TEST(ManagedMap, replacement_skips_destructor) {
  Count destructions = 0;
  {
    Allocator::Arena arena;
    Managed::Map<Count, ManagedMapValue> values(arena);
    ManagedMapValue first(1, destructions);
    ManagedMapValue second(2, destructions);

    EXPECT(values.insert(1, first));
    EXPECT_EQ((*values.assign(1, second)).get_value(), Count(2));
    EXPECT_EQ(destructions, Count(0));
  }

  EXPECT_EQ(destructions, Count(2));
}

VALIDATION_TEST(ManagedMap, insert_stress_test) {
  Allocator::Arena arena;
  Managed::Map<S32, S32> values(arena);
  for (Count i = 0; i < 1000; i++) {
    values.insert(i, i + 2);
  }

  EXPECT_EQ(values.get_size(), Count(1000));
  for (Count i = 0; i < 1000; i++) {
    ASSERT_EQ(*values[i], i + 2);
  }
}

VALIDATION_TEST(ManagedMap, growth_preserves_old_values) {
  Count destructions = 0;
  {
    Allocator::Arena arena;
    Managed::Map<Count, ManagedMapValue> values(arena);
    ManagedMapValue first(1, destructions);
    values.insert(1, first);

    auto retained = values.find(1);
    ASSERT(retained);
    const ManagedMapValue* retained_location = &*retained;

    for (Count value = 2; value < 64; value++) {
      ManagedMapValue inserted(value, destructions);
      values.insert(value, inserted);
    }

    EXPECT(retained_location->has_valid_location());
    EXPECT_EQ(retained_location->get_value(), Count(1));
    EXPECT(values.remove(1));
    values.clear();
    EXPECT_EQ(destructions, Count(62));
  }

  EXPECT_EQ(destructions, Count(63));
}
