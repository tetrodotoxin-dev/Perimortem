// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/thread/map.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/null_terminated.hpp"
#include "perimortem/core/static/bytes.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/allocator/bibliotheca.hpp"
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

template <typename value_type, typename visitor_type>
static auto expect_selected(
    Option<value_type&> selected,
    visitor_type visitor,
    Test::TestResult& result) -> void {
  selected.visit(visitor, [&] { EXPECT(false); });
}

// Map policies share insertion, lookup, replacement, removal, traversal, copy,
// capacity, and reset behavior. The factory supplies each policy's construction
// requirements so every wrapper runs the same capability contract.
template <typename factory_type, typename move_factory_type>
static auto check_map_contract(
    factory_type create,
    move_factory_type create_move_values,
    Test::TestResult& result) -> void {
  auto empty = create(0);
  EXPECT(empty.is_empty());
  EXPECT_EQ(empty.get_capacity(), Count(0));

  auto initial = create(0);
  initial.ensure_capacity(1);
  EXPECT_EQ(initial.get_capacity(), Count(14));

  const Count initial_capacity = initial.get_capacity();
  initial.ensure_capacity(initial_capacity);
  EXPECT_EQ(initial.get_capacity(), initial_capacity);

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
  expect_selected(found, [&](S32 value) { EXPECT_EQ(value, 3); }, result);
  EXPECT(!values.find(8));

  const auto& fixed = values;
  static_assert(__is_same(decltype(fixed.find(1)), Option<const S32&>));
  static_assert(__is_same(decltype(fixed[1]), Option<const S32&>));
  expect_selected(fixed[1], [&](S32 value) { EXPECT_EQ(value, 2); }, result);
  EXPECT(!fixed[8]);

  expect_selected(
      values.assign(1, 4), [&](S32 value) { EXPECT_EQ(value, 4); }, result);
  EXPECT(!values.assign(8, 4));
  EXPECT_EQ(values.find_or_create(1), 4);
  EXPECT_EQ(values.get_size(), Count(3));
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
  expect_selected(values[1], [&](S32 value) { EXPECT_EQ(value, 5); }, result);

  Count fixed_visited = 0;
  S32 fixed_value_sum = 0;
  fixed.visit([&](const S32&, const S32& value) {
    fixed_visited++;
    fixed_value_sum += value;
  });

  EXPECT_EQ(fixed_visited, Count(4));
  EXPECT_EQ(fixed_value_sum, 16);

  auto copied = values;
  EXPECT(copied.assign(1, 9));
  EXPECT(copied.insert(16, 32));
  expect_selected(values[1], [&](S32 value) { EXPECT_EQ(value, 5); }, result);
  EXPECT(!values.contains(16));

  auto assigned = create(0);
  assigned = values;
  EXPECT_EQ(assigned.get_size(), values.get_size());
  EXPECT(assigned.assign(1, 64));
  expect_selected(values[1], [&](S32 value) { EXPECT_EQ(value, 5); }, result);
  expect_selected(
      assigned[1], [&](S32 value) { EXPECT_EQ(value, 64); }, result);

  EXPECT(values.remove(2));
  EXPECT(!values.remove(2));
  EXPECT(!values.contains(2));

  const Count retained_capacity = values.get_capacity();
  values.clear();
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), retained_capacity);
  EXPECT(values.insert(32, 64));
  expect_selected(values[32], [&](S32 value) { EXPECT_EQ(value, 64); }, result);

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

  auto move_values = create_move_values(0);
  MoveOnlyValue<S32> inserted_value(4);
  EXPECT(move_values.insert(1, Data::take(inserted_value)));
  EXPECT_EQ(inserted_value.get_value(), 0);

  MoveOnlyValue<S32> duplicate_value(8);
  EXPECT(!move_values.insert(1, Data::take(duplicate_value)));
  EXPECT_EQ(duplicate_value.get_value(), 8);

  MoveOnlyValue<S32> missing_replacement(16);
  EXPECT(!move_values.assign(2, Data::take(missing_replacement)));
  EXPECT_EQ(missing_replacement.get_value(), 16);

  MoveOnlyValue<S32> replacement(32);
  auto replaced = move_values.assign(1, Data::take(replacement));
  ASSERT(replaced);
  EXPECT_EQ(replacement.get_value(), 0);
  expect_selected(
      replaced,
      [&](const MoveOnlyValue<S32>& value) {
        EXPECT_EQ(value.get_value(), 32);
      },

      result);

  // Repeated cycles combine sentinel creation, replacement, reuse, clear and
  // reset. Fixed arrays provide an independent model for every stored value
  // while the table repeatedly crosses its growth boundaries.
  constexpr Count model_size = 127;
  S32 expected[model_size] = {};
  auto sequenced = create(0);
  for (Count cycle = 0; cycle < 8; cycle++) {
    for (Count key = 0; key < model_size; key++) {
      expected[key] = S32(cycle * model_size + key + 1);
      EXPECT(sequenced.insert(S32(key), expected[key]));
    }

    for (Count key = 0; key < model_size; key += 3) {
      EXPECT(sequenced.remove(S32(key)));
      expected[key] = 0;
    }

    for (Count key = 0; key < model_size; key += 5) {
      const S32 replacement_value = S32(cycle + key + 4096);
      auto assigned_value = sequenced.assign(S32(key), replacement_value);
      if (key % 3 == 0) {
        EXPECT(!assigned_value);
      } else {
        ASSERT(assigned_value);
        expected[key] = replacement_value;
      }
    }

    for (Count key = 0; key < model_size; key += 3) {
      S32& created = sequenced.find_or_create(S32(key));
      EXPECT_EQ(created, 0);
      created = S32(cycle + key + 8192);
      expected[key] = created;
    }

    EXPECT_EQ(sequenced.get_size(), model_size);
    for (Count key = 0; key < model_size; key++) {
      auto current = sequenced.find(S32(key));
      ASSERT(current);
      expect_selected(
          current, [&](S32 value) { EXPECT_EQ(value, expected[key]); }, result);
    }

    if (cycle % 2 == 0) {
      const Count retained_capacity = sequenced.get_capacity();
      sequenced.clear();
      EXPECT_EQ(sequenced.get_capacity(), retained_capacity);
    } else {
      sequenced.reset();
      EXPECT_EQ(sequenced.get_capacity(), Count(0));
    }
  }
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
  const Count requests = Allocator::Bibliotheca::check_out_requests();
  Thread::Map<S32, S32> empty;
  EXPECT(empty.is_empty());
  EXPECT_EQ(Allocator::Bibliotheca::check_out_requests(), requests);

  check_map_contract(
      [](Count capacity) { return Thread::Map<S32, S32>(capacity); },
      [](Count capacity) {
        return Thread::Map<S32, MoveOnlyValue<S32>>(capacity);
      },

      result);
}

VALIDATION_TEST(ThreadMap, nonassignable_value_replacement) {
  Thread::Map<S32, NonAssignableValue> values;
  NonAssignableValue first(4);
  NonAssignableValue second(8);

  EXPECT(values.insert(1, first));
  expect_selected(
      values.assign(1, second),
      [&](const NonAssignableValue& value) { EXPECT_EQ(value.get_value(), 8); },
      result);

  auto assigned = values.assign(1, first);
  ASSERT(assigned);
  expect_selected(
      assigned,
      [&](const NonAssignableValue& value) { EXPECT_EQ(value.get_value(), 4); },
      result);
}

VALIDATION_TEST(ThreadMap, replacement_materializes_before_retirement) {
  Thread::Map<S32, AliasedReplacementValue> values;
  values.insert(1, "keep"_view);

  View::Bytes alias;
  expect_selected(
      values[1],
      [&](const AliasedReplacementValue& value) { alias = value.get_view(); },
      result);

  auto replaced = values.assign(1, alias);

  ASSERT(replaced);
  expect_selected(
      replaced,
      [&](const AliasedReplacementValue& value) {
        ASSERT_TEXT(value.get_view(), "keep"_view);
      },

      result);
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
  expect_selected(
      values[ProbeKey(Count(-1), 24)],
      [&](Count value) { EXPECT_EQ(value, Count(24)); }, result);
}

VALIDATION_TEST(ThreadMap, duplicate_empty_keys) {
  Thread::Map<Thread::Bytes, S32> empty_map;

  Count i = 0;
  while (i < 1000) {
    i++;
    empty_map.find_or_create(Thread::Bytes()) = i;
  }

  EXPECT_EQ(empty_map.get_size(), 1);
  expect_selected(
      empty_map[Thread::Bytes()], [&](S32 value) { EXPECT_EQ(value, i); },
      result);
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
      expect_selected(
          custom_map[TrackedValue(i, counts)],
          [&](S32 value) { ASSERT_EQ(value, i); }, result);
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
      expect_selected(
          custom_map[i],
          [&](const TrackedValue& value) {
            ASSERT(value == TrackedValue(i, counts));
          },

          result);
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

  expect_selected(
      text_map[hello], [&](S32 value) { ASSERT_EQ(value, 0); }, result);
  expect_selected(
      text_map[world], [&](S32 value) { ASSERT_EQ(value, 1); }, result);
  expect_selected(
      text_map[longer], [&](S32 value) { ASSERT_EQ(value, 2); }, result);
  for (U8 ch = 'A'; ch < 'z'; ch++) {
    text.get_access().get_data()[0] = ch;
    expect_selected(
        text_map[text], [&](S32 value) { ASSERT_EQ(value, 2 + ch); }, result);
  }

  expect_selected(
      text_map[longer], [&](S32 value) { ASSERT_EQ(value, 2); }, result);

  auto copied = text_map;
  expect_selected(
      copied[hello], [&](S32 value) { ASSERT_EQ(value, 0); }, result);
  expect_selected(
      copied[longer], [&](S32 value) { ASSERT_EQ(value, 2); }, result);
}

VALIDATION_TEST(ThreadMap, dynamic_value) {
  Thread::Map<S32, Thread::Bytes> text_map;

  text_map.insert(0, Thread::Bytes("Hello"_view));
  text_map.insert(1, Thread::Bytes("World"_view));
  text_map.insert(2, Thread::Bytes("Longer test string"_view));

  expect_selected(
      text_map[0],
      [&](const Thread::Bytes& value) {
        ASSERT_TEXT(value.get_view(), "Hello"_view);
      },

      result);
  expect_selected(
      text_map[1],
      [&](const Thread::Bytes& value) {
        ASSERT_TEXT(value.get_view(), "World"_view);
      },

      result);
  expect_selected(
      text_map[2],
      [&](const Thread::Bytes& value) {
        ASSERT_TEXT(value.get_view(), "Longer test string"_view);
      },

      result);

  auto copied = text_map;
  expect_selected(
      copied[0],
      [&](const Thread::Bytes& value) {
        ASSERT_TEXT(value.get_view(), "Hello"_view);
      },

      result);
  expect_selected(
      copied[2],
      [&](const Thread::Bytes& value) {
        ASSERT_TEXT(value.get_view(), "Longer test string"_view);
      },

      result);
}

VALIDATION_TEST(ThreadMap, insertion_alias) {
  Thread::Map<S32, Thread::Bytes> values(1);
  const Count capacity = values.get_capacity();
  values.insert(0, Thread::Bytes("retained"_view));
  for (Count key = 1; key < capacity; key++) {
    values.insert(S32(key), Thread::Bytes("filler"_view));
  }

  const Thread::Bytes* alias = nullptr;
  expect_selected(
      values[0], [&](const Thread::Bytes& value) { alias = &value; }, result);
  ASSERT(alias != nullptr);
  values.insert(S32(capacity), *alias);

  expect_selected(
      values[0],
      [&](const Thread::Bytes& value) {
        ASSERT_TEXT(value.get_view(), "retained"_view);
      },

      result);
  expect_selected(
      values[S32(capacity)],
      [&](const Thread::Bytes& value) {
        ASSERT_TEXT(value.get_view(), "retained"_view);
      },

      result);
}

VALIDATION_TEST(ThreadMap, default_insertion_alias) {
  Thread::Map<Thread::Bytes, Thread::Bytes> values(1);
  Thread::Bytes source_key("source key"_view);
  values.insert(source_key, Thread::Bytes("derived key"_view));

  const Thread::Bytes* alias = nullptr;
  expect_selected(
      values[source_key], [&](const Thread::Bytes& value) { alias = &value; },
      result);
  ASSERT(alias != nullptr);

  Thread::Bytes& inserted = values.find_or_create(*alias);

  EXPECT(inserted.is_empty());
  EXPECT(values.contains(Thread::Bytes("derived key"_view)));
  expect_selected(
      values[source_key],
      [&](const Thread::Bytes& value) {
        ASSERT_TEXT(value.get_view(), "derived key"_view);
      },

      result);
}

VALIDATION_TEST(ThreadMap, rvalue_insert_alias) {
  Thread::Map<S32, Thread::Bytes> values(1);
  const Count capacity = values.get_capacity();
  values.insert(0, Thread::Bytes("transferred"_view));
  for (Count key = 1; key < capacity; key++) {
    values.insert(S32(key), Thread::Bytes("filler"_view));
  }

  Thread::Bytes* alias = nullptr;
  expect_selected(
      values[0], [&](Thread::Bytes& value) { alias = &value; }, result);
  ASSERT(alias != nullptr);
  values.insert(S32(capacity), Data::take(*alias));

  expect_selected(
      values[0], [&](const Thread::Bytes& value) { EXPECT(value.is_empty()); },
      result);
  expect_selected(
      values[S32(capacity)],
      [&](const Thread::Bytes& value) {
        ASSERT_TEXT(value.get_view(), "transferred"_view);
      },

      result);
}

VALIDATION_TEST(ThreadMap, assignment) {
  Thread::Map<S32, S32> source;
  source.insert(1, 2);
  source.insert(3, 4);

  Thread::Map<S32, S32> destination;
  destination.insert(5, 6);

  destination = source;

  expect_selected(source[1], [](S32& value) { value = 8; }, result);

  EXPECT_EQ(destination.get_size(), Count(2));
  expect_selected(
      destination[1], [&](S32 value) { EXPECT_EQ(value, 2); }, result);
  expect_selected(
      destination[3], [&](S32 value) { EXPECT_EQ(value, 4); }, result);
}

VALIDATION_TEST(ThreadMap, move_assignment) {
  Thread::Map<S32, S32> source;
  source.insert(1, 2);
  source.insert(3, 4);

  Thread::Map<S32, S32> destination;
  destination.insert(5, 6);

  destination = Data::take(source);

  EXPECT_EQ(destination.get_size(), Count(2));
  expect_selected(
      destination[1], [&](S32 value) { EXPECT_EQ(value, 2); }, result);
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
      [](Count capacity) { return Global::Map<S32, S32>(capacity); },
      [](Count capacity) {
        return Global::Map<S32, MoveOnlyValue<S32>>(capacity);
      },

      result);
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
    expect_selected(
        found,
        [&](const GlobalAllocationValue& value) {
          EXPECT_EQ(value.get_value(), key);
          EXPECT(value.has_valid_location());
        },

        result);
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
  expect_selected(
      values["Hello"_view], [&](S32 value) { EXPECT_EQ(value, 1); }, result);
  expect_selected(
      values["World"_view], [&](S32 value) { EXPECT_EQ(value, 2); }, result);
  expect_selected(
      values["Longer test string"_view],
      [&](S32 value) { EXPECT_EQ(value, 3); }, result);
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
  expect_selected(
      destination[1], [&](S32 value) { EXPECT_EQ(value, 2); }, result);
  expect_selected(source[1], [](S32& value) { value = 8; }, result);
  expect_selected(
      destination[1], [&](S32 value) { EXPECT_EQ(value, 2); }, result);
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
  const Count requests = Allocator::Bibliotheca::check_out_requests();
  Managed::Map<S32, S32> empty(arena);
  EXPECT(empty.is_empty());
  EXPECT_EQ(Allocator::Bibliotheca::check_out_requests(), requests);

  check_map_contract(
      [&](Count capacity) { return Managed::Map<S32, S32>(arena, capacity); },
      [&](Count capacity) {
        return Managed::Map<S32, MoveOnlyValue<S32>>(arena, capacity);
      },

      result);

  Managed::Map<S32, S32> values(arena);
  EXPECT(&values.get_arena() == &arena);
  values.insert(1, 2);

  auto copied = values;
  EXPECT(&copied.get_arena() == &arena);

  auto retained = values.find(1);
  ASSERT(retained);

  S32* retained_value = nullptr;
  expect_selected(
      retained, [&](S32& value) { retained_value = &value; }, result);
  ASSERT(retained_value != nullptr);

  Managed::Map<S32, S32> moved(Data::take(values));
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), Count(0));
  EXPECT(&moved.get_arena() == &arena);

  auto moved_value = moved.find(1);
  ASSERT(moved_value);
  expect_selected(
      moved_value, [&](S32& value) { EXPECT(&value == retained_value); },
      result);
}

VALIDATION_TEST(ManagedMap, replacement_skips_destructor) {
  ArenaRetainedValue::Counts counts;
  {
    Allocator::Arena arena;
    Managed::Map<Count, ArenaRetainedValue> values(arena);
    ArenaRetainedValue first(1, counts);
    ArenaRetainedValue second(2, counts);

    EXPECT(values.insert(1, first));
    expect_selected(
        values.assign(1, second),
        [&](const ArenaRetainedValue& value) {
          EXPECT_EQ(value.get_value(), Count(2));
        },

        result);
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
    const ArenaRetainedValue* retained_location = nullptr;
    expect_selected(
        retained,
        [&](const ArenaRetainedValue& value) { retained_location = &value; },
        result);
    ASSERT(retained_location != nullptr);

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
