// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/thread/set.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/hash.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/allocator/bibliotheca.hpp"
#include "perimortem/memory/global/set.hpp"
#include "perimortem/memory/managed/bytes.hpp"
#include "perimortem/memory/managed/set.hpp"
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

static Harness ThreadSet = {
  .name = "Thread::Set",
};

template <typename value_type, typename visitor_type>
static auto expect_selected(
    Storage::Option<value_type&> selected,
    visitor_type visitor,
    Test::TestResult& result) -> void {
  selected.visit(visitor, [&] { EXPECT(false); });
}

// Set policies share insertion, lookup, removal, traversal, copy, capacity, and
// reset behavior. The factory supplies each policy's construction requirements
// so every wrapper runs the same capability contract.
template <typename factory_type, typename move_factory_type>
static auto check_set_contract(
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
  static_assert(
      __is_same(decltype(values.find(1)), Storage::Option<const S32&>));

  EXPECT(values.insert(1));
  EXPECT(values.insert(2));
  EXPECT(values.insert(4));
  EXPECT(!values.insert(1));
  EXPECT_EQ(values.get_size(), Count(3));
  EXPECT(values.contains(1));
  EXPECT(!values.contains(8));

  auto found = values.find(2);
  ASSERT(found);
  expect_selected(found, [&](S32 value) { EXPECT_EQ(value, 2); }, result);
  EXPECT(!values.find(8));

  const auto& fixed = values;
  auto fixed_found = fixed.find(4);
  ASSERT(fixed_found);
  expect_selected(fixed_found, [&](S32 value) { EXPECT_EQ(value, 4); }, result);

  S32 sum = 0;
  values.visit([&sum](const S32& value) { sum += value; });
  EXPECT_EQ(sum, 7);

  auto copied = values;
  EXPECT(copied.insert(16));
  EXPECT(!values.contains(16));

  auto assigned = create(0);
  assigned = values;
  EXPECT_EQ(assigned.get_size(), values.get_size());
  EXPECT(assigned.insert(32));
  EXPECT(!values.contains(32));

  EXPECT(values.remove(2));
  EXPECT(!values.remove(2));
  EXPECT(!values.contains(2));

  const Count retained_capacity = values.get_capacity();
  values.clear();
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), retained_capacity);
  EXPECT(values.insert(32));
  EXPECT(values.contains(32));

  values.reset();
  values.reset();
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), Count(0));

  auto assigned_empty = create(32);
  assigned_empty.insert(1);
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
    EXPECT(capacity_values.insert(key));
  }

  EXPECT_EQ(capacity_values.get_size(), capacity);
  EXPECT_EQ(capacity_values.get_capacity(), capacity);
  EXPECT(capacity_values.insert(capacity));
  EXPECT(capacity_values.get_capacity() > capacity);

  auto move_values = create_move_values(0);
  MoveOnlyValue<S32> inserted_value(4);
  EXPECT(move_values.insert(Data::take(inserted_value)));
  EXPECT_EQ(inserted_value.get_value(), 0);

  MoveOnlyValue<S32> duplicate_value(4);
  EXPECT(!move_values.insert(Data::take(duplicate_value)));
  EXPECT_EQ(duplicate_value.get_value(), 4);
  EXPECT(move_values.contains(MoveOnlyValue<S32>(4)));
  EXPECT(move_values.remove(MoveOnlyValue<S32>(4)));
  EXPECT(move_values.is_empty());

  // Repeated cycles combine sentinel creation, reuse, clear and reset. Exact
  // membership checks after every phase verify each rebuilt probe table without
  // depending on its bucket layout.
  constexpr Count model_size = 127;
  auto sequenced = create(0);
  for (Count cycle = 0; cycle < 8; cycle++) {
    for (Count key = 0; key < model_size; key++) {
      EXPECT(sequenced.insert(S32(key)));
    }

    for (Count key = 0; key < model_size; key += 3) {
      EXPECT(sequenced.remove(S32(key)));
    }

    for (Count key = 0; key < model_size; key++) {
      EXPECT_EQ(sequenced.contains(S32(key)), key % 3 != 0);
    }

    for (Count key = 0; key < model_size; key += 3) {
      EXPECT(sequenced.insert(S32(key)));
    }

    EXPECT_EQ(sequenced.get_size(), model_size);
    for (Count key = 0; key < model_size; key++) {
      EXPECT(sequenced.contains(S32(key)));
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

template <typename key_type>
static constexpr Bool supports_copy_insert =
    requires(Thread::Set<key_type>& values, const key_type& key) {
      values.insert(key);
    };

template <typename key_type>
static constexpr Bool supports_move_insert =
    requires(Thread::Set<key_type>& values, key_type& key) {
      values.insert(Data::take(key));
    };

template <typename set_type>
static constexpr Bool supports_copy_assignment =
    requires(set_type& destination, const set_type& source) {
      destination = source;
    };

using MoveOnlySet = Thread::Set<MoveOnlyValue<S32>>;
static_assert(!supports_copy_insert<MoveOnlyValue<S32>>);
static_assert(supports_move_insert<MoveOnlyValue<S32>>);
static_assert(!__is_constructible(MoveOnlySet, const MoveOnlySet&));
static_assert(!supports_copy_assignment<MoveOnlySet>);
static_assert(__is_constructible(MoveOnlySet, MoveOnlySet&&));

// Structural mutation can rebuild probe storage during the callback. The
// contract guarantees safe mutation and a valid resulting Set without
// promising which remaining keys the current traversal will visit.
VALIDATION_TEST(ThreadSet, visit_structural_mutation) {
  Thread::Set<S32> values(1);
  const Count capacity = values.get_capacity();
  for (Count key = 0; key < capacity; key++) {
    values.insert(S32(key));
  }

  Bool mutated = False;
  S32 removed_key = 0;
  values.visit([&](const S32& key) {
    if (!mutated) {
      mutated = True;
      removed_key = key;
      values.insert(S32(capacity));
      values.remove(removed_key);
    }
  });

  EXPECT(mutated);
  EXPECT_EQ(values.get_size(), capacity);
  EXPECT(values.contains(S32(capacity)));
  EXPECT(!values.contains(removed_key));
}

VALIDATION_TEST(ThreadSet, capability_contract) {
  const Count requests = Allocator::Bibliotheca::check_out_requests();
  Thread::Set<S32> empty;
  EXPECT(empty.is_empty());
  EXPECT_EQ(Allocator::Bibliotheca::check_out_requests(), requests);

  check_set_contract(
      [](Count capacity) { return Thread::Set<S32>(capacity); },
      [](Count capacity) { return Thread::Set<MoveOnlyValue<S32>>(capacity); },
      result);
}

VALIDATION_TEST(ThreadSet, remove_collisions) {
  Thread::Set<ProbeKey> values;
  for (Count i = 0; i < 24; i++) {
    EXPECT(values.insert(ProbeKey(Count(-1), i)));
  }

  const Count capacity = values.get_capacity();
  EXPECT(values.remove(ProbeKey(Count(-1), 8)));
  EXPECT(!values.contains(ProbeKey(Count(-1), 8)));
  for (Count i = 0; i < 24; i++) {
    if (i == 8) {
      continue;
    }

    ASSERT(values.contains(ProbeKey(Count(-1), i)));
  }

  EXPECT(!values.insert(ProbeKey(Count(-1), 16)));
  EXPECT(values.insert(ProbeKey(Count(-1), 8)));
  EXPECT(values.contains(ProbeKey(Count(-1), 8)));
  EXPECT_EQ(values.get_capacity(), capacity);
}

VALIDATION_TEST(ThreadSet, remove_displacements) {
  EXPECT_EQ(Hash(ProbeKey(3, 99)).get_value(), 3);

  {
    Thread::Set<ProbeKey> values(8);

    EXPECT(values.insert(ProbeKey(0, 0)));
    EXPECT(values.insert(ProbeKey(0, 1)));
    EXPECT(values.insert(ProbeKey(1, 2)));

    EXPECT(values.remove(ProbeKey(0, 0)));
    EXPECT(!values.contains(ProbeKey(0, 0)));
    EXPECT(values.contains(ProbeKey(0, 1)));
    EXPECT(values.contains(ProbeKey(1, 2)));
  }

  {
    Thread::Set<ProbeKey> values(8);

    EXPECT(values.insert(ProbeKey(0, 0)));
    EXPECT(values.insert(ProbeKey(1, 1)));
    EXPECT(values.insert(ProbeKey(2, 2)));
    EXPECT(values.insert(ProbeKey(3, 3)));
    EXPECT(values.insert(ProbeKey(0, 4)));

    EXPECT(values.remove(ProbeKey(0, 0)));
    EXPECT(!values.contains(ProbeKey(0, 0)));
    EXPECT(values.contains(ProbeKey(1, 1)));
    EXPECT(values.contains(ProbeKey(2, 2)));
    EXPECT(values.contains(ProbeKey(3, 3)));
    EXPECT(values.contains(ProbeKey(0, 4)));
  }
}

VALIDATION_TEST(ThreadSet, dynamic_keys) {
  Thread::Set<Thread::Bytes> values;
  Thread::Bytes hello("Hello"_view);
  Thread::Bytes world("World"_view);
  Thread::Bytes missing("Missing"_view);

  EXPECT(values.insert(hello));
  EXPECT(values.insert(world));
  EXPECT(!values.insert(hello));
  EXPECT(values.contains(hello));
  EXPECT(values.contains(world));
  EXPECT(!values.contains(missing));
  EXPECT_EQ(values.get_size(), 2);
}

VALIDATION_TEST(ThreadSet, remove_dynamic) {
  Thread::Set<Thread::Bytes> values;
  Thread::Bytes hello("Hello"_view);
  Thread::Bytes world("World"_view);
  Thread::Bytes longer("Longer test string"_view);

  values.insert(hello);
  values.insert(world);
  values.insert(longer);

  EXPECT(values.remove(world));
  EXPECT(values.contains(hello));
  EXPECT(!values.contains(world));
  EXPECT(values.contains(longer));
  EXPECT_EQ(values.get_size(), 2);
}

VALIDATION_TEST(ThreadSet, pointer_keys) {
  class StableObject {
   public:
    StableObject(Count id) : id(id) {}

    constexpr auto get_id() const -> Count { return id; }

   private:
    Count id = 0;
  };

  class StableObjectKey {
   public:
    StableObjectKey(const StableObject* object) : object(object) {}

    auto hash() const -> U64 {
      return Hash(U64(Data::address(object))).get_value();
    }

    constexpr auto operator==(const StableObjectKey& rhs) const -> Bool {
      return object == rhs.object;
    }

    constexpr auto get_object() const -> const StableObject* { return object; }

   private:
    const StableObject* object = nullptr;
  };

  StableObject first(1);
  StableObject second(2);
  StableObject third(3);

  Thread::Set<StableObjectKey> values;
  EXPECT(values.insert(&first));
  EXPECT(values.insert(&second));
  EXPECT(values.insert(&third));
  EXPECT(!values.insert(&second));

  auto found = values.find(&second);
  ASSERT(found);
  expect_selected(
      found,
      [&](const StableObjectKey& key) {
        EXPECT(key.get_object() == &second);
        EXPECT_EQ(key.get_object()->get_id(), 2);
      },

      result);

  EXPECT(values.remove(&second));
  EXPECT(!values.contains(&second));
  EXPECT(values.contains(&first));
  EXPECT(values.contains(&third));
}

VALIDATION_TEST(ThreadSet, key_construction) {
  TrackedValue::Counts counts;

  {
    Thread::Set<TrackedValue> values;
    for (Count i = 0; i < 100; i++) {
      values.insert(TrackedValue(i, counts));
    }

    EXPECT_EQ(values.get_size(), 100);
    for (Count i = 0; i < 100; i++) {
      ASSERT(values.contains(TrackedValue(i, counts)));
    }
  }

  EXPECT_EQ(counts.constructions, counts.destructions);
}

VALIDATION_TEST(ThreadSet, leak_test) {
  auto pre_test_memory = Allocator::Bibliotheca::allocated_memory();

  {
    Thread::Set<Thread::Bytes> memory_intensive;
    Thread::Bytes source{};
    for (Count i = 0; i < 100; i++) {
      source.append('A');
      memory_intensive.insert(source);
      if (i % 2 == 0) {
        memory_intensive.remove(source);
      }
    }

    ASSERT_EQ(memory_intensive.get_size(), 50);
  }

  {
    Thread::Set<S32> large_set;
    for (Count i = 0; i < 1000; i++) {
      large_set.insert(i);
    }
  }

  {
    Thread::Set<Thread::Bytes> first;
    Thread::Set<Thread::Bytes> second;
    Thread::Bytes old_block("old block"_view);
    Thread::Bytes new_block("new block"_view);

    first.insert(old_block);
    second.insert(new_block);
    first = Data::take(second);
    ASSERT(first.contains(new_block));
    ASSERT(!first.contains(old_block));
  }

  auto post_test_memory = Allocator::Bibliotheca::allocated_memory();
  EXPECT_EQ(pre_test_memory, post_test_memory);
}

static_assert(__is_constructible(Global::Set<U32>));
static_assert(!__is_convertible_to(Count, Global::Set<U32>));

static Harness GlobalSet = {
  .name = "Global::Set",
};

static GlobalAllocationValue::Counts global_set_counts;
static Global::Set<GlobalAllocationValue> transferred_set;

static auto create_global_set(View::Bytes) -> void {
  Global::Set<GlobalAllocationValue> created;
  for (U32 value = 0; value < 1024; value++) {
    created.insert(GlobalAllocationValue(value, global_set_counts));
  }

  transferred_set = Data::take(created);
}

VALIDATION_TEST(GlobalSet, capability_contract) {
  check_set_contract(
      [](Count capacity) { return Global::Set<S32>(capacity); },
      [](Count capacity) { return Global::Set<MoveOnlyValue<S32>>(capacity); },
      result);
}

VALIDATION_TEST(GlobalSet, cross_thread_reallocation) {
  global_set_counts = {};

  auto worker = Process::Worker::start(
      "global.set"_view, create_global_set, View::Bytes());
  worker.join();

  EXPECT_EQ(transferred_set.get_size(), Count(1024));
  EXPECT_EQ(global_set_counts.copies, Count(0));
  EXPECT(global_set_counts.moves > transferred_set.get_size());

  Count visited = 0;
  transferred_set.visit([&](const GlobalAllocationValue& value) {
    visited++;
    EXPECT(value.has_valid_location());
    EXPECT(value.get_value() < 1024);
  });

  EXPECT_EQ(visited, Count(1024));

  // Reset releases worker allocated storage from this thread.
  transferred_set.reset();
  EXPECT(transferred_set.is_empty());
  EXPECT_EQ(transferred_set.get_capacity(), Count(0));
  EXPECT_EQ(global_set_counts.releases, Count(1024));
}

static_assert(!__is_constructible(Managed::Set<U32>));
static_assert(__is_constructible(Managed::Set<U32>, Allocator::Arena&));

static Harness ManagedSet = {
  .name = "Managed::Set",
};

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

VALIDATION_TEST(ManagedSet, capability_contract) {
  Allocator::Arena arena;
  const Count requests = Allocator::Bibliotheca::check_out_requests();
  Managed::Set<S32> empty(arena);
  EXPECT(empty.is_empty());
  EXPECT_EQ(Allocator::Bibliotheca::check_out_requests(), requests);

  check_set_contract(
      [&](Count capacity) { return Managed::Set<S32>(arena, capacity); },
      [&](Count capacity) {
        return Managed::Set<MoveOnlyValue<S32>>(arena, capacity);
      },

      result);

  Managed::Set<S32> values(arena);
  EXPECT(&values.get_arena() == &arena);
  values.insert(4);

  auto copied = values;
  EXPECT(&copied.get_arena() == &arena);

  auto retained = values.find(4);
  ASSERT(retained);

  const S32* retained_key = nullptr;
  expect_selected(
      retained, [&](const S32& value) { retained_key = &value; }, result);
  ASSERT(retained_key != nullptr);

  Managed::Set<S32> moved(Data::take(values));
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), Count(0));
  EXPECT(&moved.get_arena() == &arena);

  auto moved_key = moved.find(4);
  ASSERT(moved_key);
  expect_selected(
      moved_key, [&](const S32& value) { EXPECT(&value == retained_key); },
      result);
}

VALIDATION_TEST(ManagedSet, growth_preserves_old_values) {
  ArenaRetainedValue::Counts counts;
  {
    Allocator::Arena arena;
    Managed::Set<ArenaRetainedValue> values(arena);
    ArenaRetainedValue first(1, counts);
    values.insert(first);

    auto retained = values.find(first);
    ASSERT(retained);
    const ArenaRetainedValue* retained_location = nullptr;
    expect_selected(
        retained,
        [&](const ArenaRetainedValue& value) { retained_location = &value; },
        result);
    ASSERT(retained_location != nullptr);

    for (Count value = 2; value < 64; value++) {
      ArenaRetainedValue inserted(value, counts);
      values.insert(inserted);
    }

    EXPECT(retained_location->has_valid_location());
    EXPECT(values.remove(first));
    values.clear();
    EXPECT_EQ(counts.destructions, Count(62));
  }

  EXPECT_EQ(counts.destructions, Count(63));
}
