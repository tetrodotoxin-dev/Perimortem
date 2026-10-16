// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/thread/set.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/hash.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/memory/thread/bytes.hpp"
#include "validation/tests/memory/hashable.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Toolchain::Validation;

static Harness ThreadSet = {
  .name = "Thread::Set",
  .setup =
      []() {
        default_construct_count = 0;
        default_destruct_count = 0;
      },
};

class MoveOnlyKey {
 public:
  MoveOnlyKey(S32 value) : value(value) {}

  MoveOnlyKey(const MoveOnlyKey&) = delete;
  MoveOnlyKey(MoveOnlyKey&&) = default;

  constexpr auto hash() const -> U64 { return Hash(value).get_value(); }
  constexpr auto operator==(const MoveOnlyKey& rhs) const -> Bool {
    return value == rhs.value;
  }

 private:
  S32 value;
};

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

using MoveOnlySet = Thread::Set<MoveOnlyKey>;
static_assert(!supports_copy_insert<MoveOnlyKey>);
static_assert(supports_move_insert<MoveOnlyKey>);
static_assert(!__is_constructible(MoveOnlySet, const MoveOnlySet&));
static_assert(!supports_copy_assignment<MoveOnlySet>);
static_assert(__is_constructible(MoveOnlySet, MoveOnlySet&&));

VALIDATION_TEST(ThreadSet, unique_values) {
  Thread::Set<S32> values;

  EXPECT(values.insert(1));
  EXPECT(!values.insert(1));
  EXPECT(values.insert(4));
  EXPECT(values.contains(1));
  EXPECT(values.contains(4));
  EXPECT(!values.contains(2));
  EXPECT_EQ(values.get_size(), 2);

  values.clear();
  EXPECT(values.is_empty());
  EXPECT(!values.contains(1));
}

VALIDATION_TEST(ThreadSet, find) {
  Thread::Set<S32> values(4);

  values.insert(1);
  values.insert(2);
  values.insert(4);

  static_assert(__is_same(decltype(values.find(2)), Option<const S32&>));
  auto found = values.find(2);
  ASSERT(found);
  EXPECT_EQ(*found, 2);
  EXPECT(!values.find(8));

  const auto& const_values = values;
  auto const_found = const_values.find(4);
  ASSERT(const_found);
  EXPECT_EQ(*const_found, 4);
  EXPECT(!const_values.find(8));
}

VALIDATION_TEST(ThreadSet, visit) {
  Thread::Set<S32> values;
  values.insert(1);
  values.insert(2);
  values.insert(4);

  S32 total = 0;
  values.visit([&total](const S32& value) { total += value; });
  EXPECT_EQ(total, 7);

  total = 0;

  const Thread::Set<S32>& const_values = values;
  const_values.visit([&total](const S32& value) { total += value; });
  EXPECT_EQ(total, 7);
}

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

VALIDATION_TEST(ThreadSet, logical_capacity) {
  Thread::Set<S32> values;
  EXPECT_EQ(values.get_capacity(), Count(0));

  values.ensure_capacity(1000);

  const Count capacity = values.get_capacity();
  EXPECT(capacity >= 1000);

  values.ensure_capacity(capacity);
  EXPECT_EQ(values.get_capacity(), capacity);

  for (Count key = 0; key < capacity; key++) {
    EXPECT(values.insert(key));
  }

  EXPECT_EQ(values.get_capacity(), capacity);
  EXPECT(values.insert(capacity));
  EXPECT(values.get_capacity() > capacity);
}

VALIDATION_TEST(ThreadSet, clear_and_reset) {
  Thread::Set<S32> values(32);
  const Count capacity = values.get_capacity();

  values.insert(1);

  values.clear();
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), capacity);

  values.reset();
  values.reset();
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), Count(0));
}

VALIDATION_TEST(ThreadSet, empty_assignment_resets_capacity) {
  Thread::Set<S32> empty;
  Thread::Set<S32> values(32);
  values.insert(1);

  values = empty;

  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), Count(0));
}

VALIDATION_TEST(ThreadSet, move_only_keys) {
  Thread::Set<MoveOnlyKey> values;
  for (S32 value = 0; value < 100; value++) {
    EXPECT(values.insert(MoveOnlyKey(value)));
  }

  EXPECT(!values.insert(MoveOnlyKey(42)));
  EXPECT(values.contains(MoveOnlyKey(42)));
  EXPECT(values.remove(MoveOnlyKey(42)));
  EXPECT(!values.contains(MoveOnlyKey(42)));
}

VALIDATION_TEST(ThreadSet, remove) {
  Thread::Set<S32> values;

  values.insert(1);
  values.insert(2);
  values.insert(4);

  EXPECT(values.remove(2));
  EXPECT(!values.remove(2));
  EXPECT_EQ(values.get_size(), 2);
  EXPECT(values.contains(1));
  EXPECT(!values.contains(2));
  EXPECT(values.contains(4));

  EXPECT(values.insert(2));
  EXPECT(values.contains(2));
}

VALIDATION_TEST(ThreadSet, remove_collisions) {
  class CollisionKey {
   public:
    CollisionKey(Count value) : value(value) {}

    constexpr auto hash() const -> U64 { return U64(-1); }
    constexpr auto operator==(const CollisionKey& rhs) const -> Bool {
      return value == rhs.value;
    }

   private:
    Count value = 0;
  };

  Thread::Set<CollisionKey> values;
  for (Count i = 0; i < 24; i++) {
    EXPECT(values.insert(CollisionKey(i)));
  }

  const Count capacity = values.get_capacity();
  EXPECT(values.remove(CollisionKey(8)));
  EXPECT(!values.contains(CollisionKey(8)));
  for (Count i = 0; i < 24; i++) {
    if (i == 8) {
      continue;
    }

    ASSERT(values.contains(CollisionKey(i)));
  }

  EXPECT(!values.insert(CollisionKey(16)));
  EXPECT(values.insert(CollisionKey(8)));
  EXPECT(values.contains(CollisionKey(8)));
  EXPECT_EQ(values.get_capacity(), capacity);
}

VALIDATION_TEST(ThreadSet, remove_displacements) {
  class ProbeKey {
   public:
    ProbeKey(Count home, Count id) : home(home), id(id) {}

    // Tests can build exact probe chains because Core::Hash delegates to this
    // hook for custom key types.
    constexpr auto hash() const -> U64 { return home; }
    constexpr auto operator==(const ProbeKey& rhs) const -> Bool {
      return home == rhs.home && id == rhs.id;
    }

   private:
    Count home = 0;
    Count id = 0;
  };

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

VALIDATION_TEST(ThreadSet, insert_stress_test) {
  Thread::Set<S32> values;
  for (Count i = 0; i < 1000; i++) {
    EXPECT(values.insert(i));
  }

  EXPECT_EQ(values.get_size(), 1000);
  for (Count i = 0; i < 1000; i++) {
    ASSERT(values.contains(i));
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
  EXPECT((*found).get_object() == &second);
  EXPECT_EQ((*found).get_object()->get_id(), 2);

  EXPECT(values.remove(&second));
  EXPECT(!values.contains(&second));
  EXPECT(values.contains(&first));
  EXPECT(values.contains(&third));
}

VALIDATION_TEST(ThreadSet, key_construction) {
  Count construct_count = 0;
  Count destruct_count = 0;

  {
    Thread::Set<Hashable> values;
    for (Count i = 0; i < 100; i++) {
      values.insert(Hashable(i, construct_count, destruct_count));
    }

    EXPECT_EQ(values.get_size(), 100);
    for (Count i = 0; i < 100; i++) {
      ASSERT(values.contains(Hashable(i, construct_count, destruct_count)));
    }
  }

  EXPECT_EQ(construct_count, destruct_count);
  EXPECT_EQ(default_construct_count, default_destruct_count);
}

VALIDATION_TEST(ThreadSet, reuse) {
  Thread::Set<S32> values;
  for (S32 loops = 0; loops < 5; loops++) {
    values.clear();
    ASSERT_EQ(values.get_size(), 0);
    for (Count i = 0; i < 100; i++) {
      values.insert(i);
    }

    ASSERT_EQ(values.get_size(), 100);
    for (Count i = 0; i < 100; i++) {
      ASSERT(values.contains(i));
    }
  }
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
