// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/thread/map.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/null_terminated.hpp"
#include "perimortem/core/static/bytes.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/global/map.hpp"
#include "perimortem/memory/managed/bytes.hpp"
#include "perimortem/memory/managed/map.hpp"
#include "perimortem/memory/thread/bytes.hpp"
#include "perimortem/system/process/worker.hpp"
#include "validation/tests/memory/arena_retained_value.hpp"
#include "validation/tests/memory/global_allocation_value.hpp"
#include "validation/tests/memory/probe_key.hpp"
#include "validation/tests/value.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;
using namespace Perimortem::Validation;
using namespace Toolchain::Validation;

static Harness ThreadMap = {
  .name = "Thread::Map",
};

// Map policies share insertion, lookup, replacement, removal, traversal, copy,
// capacity, and reset behavior. The factory supplies each policy's construction
// requirements so every wrapper runs the same capability contract.
template <typename factory_type>
static auto check_map_contract(factory_type create, Test::TestResult& result)
    -> void {
  auto empty = create(0);
  EXPECT(empty.is_empty());
  EXPECT_EQ(empty.get_capacity(), Count(0));

  auto values = create(32);
  static_assert(__is_same(decltype(values.find(1)), Option<S32&>));
  static_assert(__is_same(decltype(values[1]), Option<S32&>));

  EXPECT(values.insert(1, 2));
  EXPECT(values.insert(2, 3));
  EXPECT(values.insert(4, 5));
  EXPECT(!values.insert(1, 8));
  EXPECT_EQ(values.get_size(), Count(3));
  EXPECT(values.contains(1));
  EXPECT(!values.contains(8));

  auto found = values.find(2);
  ASSERT(found);
  EXPECT_EQ(*found, 3);
  EXPECT(!values.find(8));

  const auto& fixed = values;
  static_assert(__is_same(decltype(fixed.find(1)), Option<const S32&>));
  static_assert(__is_same(decltype(fixed[1]), Option<const S32&>));
  EXPECT_EQ(*fixed[1], 2);
  EXPECT(!fixed[8]);

  EXPECT_EQ(*values.assign(1, 4), 4);
  EXPECT(!values.assign(8, 4));
  EXPECT_EQ(values.find_or_create(8), 0);

  Count visited = 0;
  S32 key_sum = 0;
  S32 value_sum = 0;
  values.visit([&](const S32& key, S32& value) {
    visited++;
    key_sum += key;
    value_sum += value;
    value++;
  });

  EXPECT_EQ(visited, Count(4));
  EXPECT_EQ(key_sum, 15);
  EXPECT_EQ(value_sum, 12);
  EXPECT_EQ(*values[1], 5);

  auto copied = values;
  EXPECT(copied.assign(1, 9));
  EXPECT(copied.insert(16, 32));
  EXPECT_EQ(*values[1], 5);
  EXPECT(!values.contains(16));

  EXPECT(values.remove(2));
  EXPECT(!values.remove(2));
  EXPECT(!values.contains(2));

  const Count retained_capacity = values.get_capacity();
  values.clear();
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), retained_capacity);

  values.reset();
  values.reset();
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), Count(0));

  auto assigned_empty = create(32);
  assigned_empty.insert(1, 2);
  assigned_empty = empty;
  EXPECT(assigned_empty.is_empty());
  EXPECT_EQ(assigned_empty.get_capacity(), Count(0));

  auto capacity_values = create(0);
  capacity_values.ensure_capacity(1000);

  const Count capacity = capacity_values.get_capacity();
  EXPECT(capacity >= 1000);
  capacity_values.ensure_capacity(capacity);
  EXPECT_EQ(capacity_values.get_capacity(), capacity);

  for (Count key = 0; key < capacity; key++) {
    EXPECT(capacity_values.insert(key, key));
  }

  EXPECT_EQ(capacity_values.get_size(), capacity);
  EXPECT_EQ(capacity_values.get_capacity(), capacity);
  EXPECT(capacity_values.insert(capacity, capacity));
  EXPECT(capacity_values.get_capacity() > capacity);
}

class NonAssignableValue {
 public:
  NonAssignableValue(S32 value) : value(value) {}

  NonAssignableValue(const NonAssignableValue&) = default;
  auto operator=(const NonAssignableValue&) -> NonAssignableValue& = delete;

  constexpr auto get_value() const -> S32 { return value; }

 private:
  S32 value;
};

template <typename key_type, typename value_type>
static constexpr Bool supports_find_or_create =
    requires(Thread::Map<key_type, value_type>& values, const key_type& key) {
      values.find_or_create(key);
    };

template <typename key_type, typename value_type>
static constexpr Bool supports_insert = requires(
    Thread::Map<key_type, value_type>& values,
    key_type& key,
    value_type& value) { values.insert(Data::take(key), Data::take(value)); };

static_assert(!supports_find_or_create<S32, NonAssignableValue>);

template <typename map_type>
static constexpr Bool supports_copy_assignment =
    requires(map_type& destination, const map_type& source) {
      destination = source;
    };

using MoveOnlyValueMap = Thread::Map<S32, MoveOnlyValue<S32>>;
static_assert(!__is_constructible(MoveOnlyValueMap, const MoveOnlyValueMap&));
static_assert(!supports_copy_assignment<MoveOnlyValueMap>);
static_assert(__is_constructible(MoveOnlyValueMap, MoveOnlyValueMap&&));

static_assert(!supports_find_or_create<MoveOnlyValue<S32>, S32>);
static_assert(!supports_insert<MoveOnlyValue<S32>, S32>);
static_assert(supports_insert<S32, MoveOnlyValue<S32>>);
static_assert(supports_find_or_create<S32, S32>);

class AliasedReplacementValue {
 public:
  AliasedReplacementValue(View::Bytes source) {
    for (Count index = 0; index < 4; index++) {
      values[index] = source[index];
    }
  }

  ~AliasedReplacementValue() {
    for (Count index = 0; index < 4; index++) {
      values[index] = 0;
    }
  }

  constexpr auto get_view() const -> View::Bytes {
    return View::Bytes(values, 4);
  }

 private:
  U8 values[4] = {};
};

// Structural mutation can rebuild probe storage during the callback. The
// contract guarantees safe mutation and a valid resulting Map without
// promising which remaining key value pairs the current traversal will visit.
VALIDATION_TEST(ThreadMap, visit_structural_mutation) {
  Thread::Map<S32, S32> values(1);
  const Count capacity = values.get_capacity();
  for (Count key = 0; key < capacity; key++) {
    values.insert(S32(key), S32(key));
  }

  Bool mutated = False;
  S32 removed_key = 0;
  values.visit([&](const S32& key, S32&) {
    if (!mutated) {
      mutated = True;
      removed_key = key;
      values.insert(S32(capacity), S32(capacity));
      values.remove(removed_key);
    }
  });

  EXPECT(mutated);
  EXPECT_EQ(values.get_size(), capacity);
  EXPECT(values.contains(S32(capacity)));
  EXPECT(!values.contains(removed_key));
}

VALIDATION_TEST(ThreadMap, capability_contract) {
  check_map_contract(
      [](Count capacity) { return Thread::Map<S32, S32>(capacity); }, result);
}

VALIDATION_TEST(ThreadMap, nonassignable_value_replacement) {
  Thread::Map<S32, NonAssignableValue> values;
  NonAssignableValue first(4);
  NonAssignableValue second(8);

  EXPECT(values.insert(1, first));
  EXPECT_EQ((*values.assign(1, second)).get_value(), 8);

  auto assigned = values.assign(1, first);
  ASSERT(assigned);
  EXPECT_EQ((*assigned).get_value(), 4);
}

VALIDATION_TEST(ThreadMap, move_only_value_replacement) {
  Thread::Map<S32, MoveOnlyValue<S32>> values;

  EXPECT(values.insert(1, MoveOnlyValue<S32>(4)));
  EXPECT_EQ((*values.assign(1, MoveOnlyValue<S32>(8))).get_value(), 8);

  auto assigned = values.assign(1, MoveOnlyValue<S32>(16));
  ASSERT(assigned);
  EXPECT_EQ((*assigned).get_value(), 16);
}

VALIDATION_TEST(ThreadMap, replacement_materializes_before_retirement) {
  Thread::Map<S32, AliasedReplacementValue> values;
  values.insert(1, "keep"_view);

  View::Bytes alias = (*values[1]).get_view();
  auto replaced = values.assign(1, alias);

  ASSERT(replaced);
  ASSERT_TEXT((*replaced).get_view(), "keep"_view);
}

VALIDATION_TEST(ThreadMap, sentinel_reuse) {
  Thread::Map<ProbeKey, Count> values(32);
  for (Count value = 0; value < 24; value++) {
    EXPECT(values.insert(ProbeKey(Count(-1), value), value));
  }

  const Count capacity = values.get_capacity();
  EXPECT(values.remove(ProbeKey(Count(-1), 8)));
  EXPECT(values.contains(ProbeKey(Count(-1), 16)));
  EXPECT(!values.insert(ProbeKey(Count(-1), 16), 32));
  EXPECT(values.insert(ProbeKey(Count(-1), 24), 24));
  EXPECT_EQ(values.get_capacity(), capacity);
  EXPECT_EQ(*values[ProbeKey(Count(-1), 24)], Count(24));
}

VALIDATION_TEST(ThreadMap, duplicate_empty_keys) {
  Thread::Map<Thread::Bytes, S32> empty_map;

  Count i = 0;
  while (i < 1000) {
    i++;
    empty_map.find_or_create(Thread::Bytes()) = i;
  }

  EXPECT_EQ(empty_map.get_size(), 1);
  EXPECT_EQ(*empty_map[Thread::Bytes()], i);
}

VALIDATION_TEST(ThreadMap, insert_stress_test) {
  Thread::Map<S32, S32> large_map;
  for (Count i = 0; i < 1000; i++) {
    large_map.insert(i, i + 2);
  }

  EXPECT_EQ(large_map.get_size(), 1000);
  for (Count i = 0; i < 1000; i++) {
    ASSERT_EQ(*large_map[i], i + 2);
  }
}

VALIDATION_TEST(ThreadMap, key_construction) {
  TrackedValue::Counts counts;

  {
    Thread::Map<TrackedValue, S32> custom_map;
    for (Count i = 0; i < 100; i++) {
      custom_map.insert(TrackedValue(i, counts), i);
    }

    EXPECT_EQ(custom_map.get_size(), 100);
    for (Count i = 0; i < 100; i++) {
      ASSERT_EQ(*custom_map[TrackedValue(i, counts)], i);
    }
  }

  EXPECT_EQ(counts.constructions, counts.destructions);
}

VALIDATION_TEST(ThreadMap, value_construction) {
  TrackedValue::Counts counts;

  {
    Thread::Map<S32, TrackedValue> custom_map;
    for (Count i = 0; i < 100; i++) {
      custom_map.insert(i, TrackedValue(i, counts));
    }

    EXPECT_EQ(custom_map.get_size(), 100);
    for (Count i = 0; i < 100; i++) {
      ASSERT(*custom_map[i] == TrackedValue(i, counts));
    }
  }

  EXPECT_EQ(counts.constructions, counts.destructions);
}

VALIDATION_TEST(ThreadMap, dynamic_keys) {
  Thread::Map<Thread::Bytes, S32> text_map;
  Thread::Bytes hello("Hello"_view);
  Thread::Bytes world("World"_view);
  Thread::Bytes longer("Longer test string"_view);
  Thread::Bytes text{};

  text_map.insert(hello, 0);
  text_map.insert(world, 1);

  text.append('a');
  for (U8 ch = 'A'; ch < 'z'; ch++) {
    text.get_access().get_data()[0] = ch;

    text_map.insert(text, 2 + ch);
  }

  text_map.insert(longer, 2);

  ASSERT_EQ(*text_map[hello], 0);
  ASSERT_EQ(*text_map[world], 1);
  ASSERT_EQ(*text_map[longer], 2);
  for (U8 ch = 'A'; ch < 'z'; ch++) {
    text.get_access().get_data()[0] = ch;
    ASSERT_EQ(*text_map[text], 2 + ch);
  }

  ASSERT_EQ(*text_map[longer], 2);

  auto copied = text_map;
  ASSERT_EQ(*copied[hello], 0);
  ASSERT_EQ(*copied[longer], 2);
}

VALIDATION_TEST(ThreadMap, dynamic_value) {
  Thread::Map<S32, Thread::Bytes> text_map;

  text_map.insert(0, Thread::Bytes("Hello"_view));
  text_map.insert(1, Thread::Bytes("World"_view));
  text_map.insert(2, Thread::Bytes("Longer test string"_view));

  ASSERT_TEXT((*text_map[0]).get_view(), "Hello"_view);
  ASSERT_TEXT((*text_map[1]).get_view(), "World"_view);
  ASSERT_TEXT((*text_map[2]).get_view(), "Longer test string"_view);

  auto copied = text_map;
  ASSERT_TEXT((*copied[0]).get_view(), "Hello"_view);
  ASSERT_TEXT((*copied[2]).get_view(), "Longer test string"_view);
}

VALIDATION_TEST(ThreadMap, insertion_alias) {
  Thread::Map<S32, Thread::Bytes> values(1);
  const Count capacity = values.get_capacity();
  values.insert(0, Thread::Bytes("retained"_view));
  for (Count key = 1; key < capacity; key++) {
    values.insert(S32(key), Thread::Bytes("filler"_view));
  }

  const Thread::Bytes& alias = *values[0];
  values.insert(S32(capacity), alias);

  ASSERT_TEXT((*values[0]).get_view(), "retained"_view);
  ASSERT_TEXT((*values[S32(capacity)]).get_view(), "retained"_view);
}

VALIDATION_TEST(ThreadMap, default_insertion_alias) {
  Thread::Map<Thread::Bytes, Thread::Bytes> values(1);
  Thread::Bytes source_key("source key"_view);
  values.insert(source_key, Thread::Bytes("derived key"_view));

  const Thread::Bytes& alias = *values[source_key];
  Thread::Bytes& inserted = values.find_or_create(alias);

  EXPECT(inserted.is_empty());
  EXPECT(values.contains(Thread::Bytes("derived key"_view)));
  ASSERT_TEXT((*values[source_key]).get_view(), "derived key"_view);
}

VALIDATION_TEST(ThreadMap, rvalue_insert_alias) {
  Thread::Map<S32, Thread::Bytes> values(1);
  const Count capacity = values.get_capacity();
  values.insert(0, Thread::Bytes("transferred"_view));
  for (Count key = 1; key < capacity; key++) {
    values.insert(S32(key), Thread::Bytes("filler"_view));
  }

  Thread::Bytes& alias = *values[0];
  values.insert(S32(capacity), Data::take(alias));

  EXPECT((*values[0]).is_empty());
  ASSERT_TEXT((*values[S32(capacity)]).get_view(), "transferred"_view);
}

VALIDATION_TEST(ThreadMap, assignment) {
  Thread::Map<S32, S32> source;
  source.insert(1, 2);
  source.insert(3, 4);

  Thread::Map<S32, S32> destination;
  destination.insert(5, 6);

  destination = source;

  *source[1] = 8;

  EXPECT_EQ(destination.get_size(), Count(2));
  EXPECT_EQ(*destination[1], 2);
  EXPECT_EQ(*destination[3], 4);
}

VALIDATION_TEST(ThreadMap, move_assignment) {
  Thread::Map<S32, S32> source;
  source.insert(1, 2);
  source.insert(3, 4);

  Thread::Map<S32, S32> destination;
  destination.insert(5, 6);

  destination = Data::take(source);

  EXPECT_EQ(destination.get_size(), Count(2));
  EXPECT_EQ(*destination[1], 2);
}

VALIDATION_TEST(ThreadMap, reuse) {
  Thread::Map<S32, S32> reuse_map;
  for (S32 loops = 0; loops < 5; loops++) {
    reuse_map.clear();
    ASSERT_EQ(reuse_map.get_size(), 0);
    for (Count i = 0; i < 100; i++) {
      reuse_map.find_or_create(i) = i;
    }

    ASSERT_EQ(reuse_map.get_size(), 100);
    for (Count i = 0; i < 100; i++) {
      ASSERT_EQ(*reuse_map[i], i);
    }
  }
}

VALIDATION_TEST(ThreadMap, leak_test) {
  auto pre_test_memory = Allocator::Bibliotheca::allocated_memory();

  {
    Thread::Map<Thread::Bytes, Thread::Bytes> memory_intensive;
    Thread::Bytes source{};
    for (Count i = 0; i < 100; i++) {
      source.append('A');

      memory_intensive.insert(source, Thread::Bytes("Test text to copy"_view));
    }

    ASSERT_EQ(memory_intensive.get_size(), 100);
    EXPECT(memory_intensive.remove(source));
    ASSERT_EQ(memory_intensive.get_size(), 99);
  }

  {
    Thread::Map<S32, S32> large_map;
    for (Count i = 0; i < 1000; i++) {
      large_map.insert(i, i + 2);
    }
  }

  auto post_test_memory = Allocator::Bibliotheca::allocated_memory();
  EXPECT_EQ(pre_test_memory, post_test_memory);
}

static_assert(__is_constructible(Global::Map<U32, U32>));
static_assert(!__is_convertible_to(Count, Global::Map<U32, U32>));

static Harness GlobalMap = {
  .name = "Global::Map",
};

static GlobalAllocationValue::Counts global_map_counts;
static Global::Map<U32, GlobalAllocationValue> transferred_map;

static auto create_global_map(View::Bytes) -> void {
  Global::Map<U32, GlobalAllocationValue> created;
  for (U32 value = 0; value < 128; value++) {
    created.insert(U32(value), GlobalAllocationValue(value, global_map_counts));
  }

  transferred_map = Data::take(created);
}

VALIDATION_TEST(GlobalMap, capability_contract) {
  check_map_contract(
      [](Count capacity) { return Global::Map<S32, S32>(capacity); }, result);
}

// Rehashing consumes former keys and values in worker allocated process
// storage. Reading and releasing the completed Map on this thread proves that
// its keys, values, and allocation can cross the worker boundary.
VALIDATION_TEST(GlobalMap, cross_thread_reallocation) {
  global_map_counts = {};

  auto worker = Process::Worker::start(
      "global.map"_view, create_global_map, View::Bytes());
  worker.join();

  EXPECT_EQ(transferred_map.get_size(), Count(128));
  EXPECT_EQ(global_map_counts.copies, Count(0));
  EXPECT(global_map_counts.moves > transferred_map.get_size());
  for (U32 key = 0; key < 128; key++) {
    auto found = transferred_map.find(key);
    ASSERT(found);
    EXPECT_EQ((*found).get_value(), key);
    EXPECT((*found).has_valid_location());
  }

  EXPECT(transferred_map.remove(64));
  EXPECT(!transferred_map.contains(64));

  transferred_map.reset();
  EXPECT(transferred_map.is_empty());
  EXPECT_EQ(transferred_map.get_capacity(), Count(0));
  EXPECT_EQ(global_map_counts.releases, Count(128));
}

static_assert(!__is_constructible(Managed::Map<U32, U32>));
static_assert(__is_constructible(Managed::Map<U32, U32>, Allocator::Arena&));

static Harness ManagedMap = {
  .name = "Managed::Map",
};

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

VALIDATION_TEST(ManagedMap, capability_contract) {
  Allocator::Arena arena;
  check_map_contract(
      [&](Count capacity) { return Managed::Map<S32, S32>(arena, capacity); },
      result);

  Managed::Map<S32, S32> values(arena);
  EXPECT(&values.get_arena() == &arena);
}

VALIDATION_TEST(ManagedMap, replacement_skips_destructor) {
  ArenaRetainedValue::Counts counts;
  {
    Allocator::Arena arena;
    Managed::Map<Count, ArenaRetainedValue> values(arena);
    ArenaRetainedValue first(1, counts);
    ArenaRetainedValue second(2, counts);

    EXPECT(values.insert(1, first));
    EXPECT_EQ((*values.assign(1, second)).get_value(), Count(2));
    EXPECT_EQ(counts.destructions, Count(0));
  }

  EXPECT_EQ(counts.destructions, Count(2));
}

VALIDATION_TEST(ManagedMap, growth_preserves_old_values) {
  ArenaRetainedValue::Counts counts;
  {
    Allocator::Arena arena;
    Managed::Map<Count, ArenaRetainedValue> values(arena);
    ArenaRetainedValue first(1, counts);
    values.insert(1, first);

    auto retained = values.find(1);
    ASSERT(retained);
    const ArenaRetainedValue* retained_location = &*retained;

    for (Count value = 2; value < 64; value++) {
      ArenaRetainedValue inserted(value, counts);
      values.insert(value, inserted);
    }

    EXPECT(retained_location->has_valid_location());
    EXPECT_EQ(retained_location->get_value(), Count(1));
    EXPECT(values.remove(1));
    values.clear();
    EXPECT_EQ(counts.destructions, Count(62));
  }

  EXPECT_EQ(counts.destructions, Count(63));
}
