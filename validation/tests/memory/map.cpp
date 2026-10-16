// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/thread/map.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/null_terminated.hpp"
#include "perimortem/core/static/bytes.hpp"
#include "perimortem/memory/thread/bytes.hpp"
#include "validation/tests/memory/hashable.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Toolchain::Validation;

static Harness ThreadMap = {
  .name = "Thread::Map",
  .setup =
      []() {
        default_construct_count = 0;
        default_destruct_count = 0;
      },
};

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

class MoveOnlyValue {
 public:
  MoveOnlyValue(S32 value) : value(value) {}

  MoveOnlyValue(const MoveOnlyValue&) = delete;
  MoveOnlyValue(MoveOnlyValue&& source) : value(source.value) {
    source.value = 0;
  }

  constexpr auto get_value() const -> S32 { return value; }

 private:
  S32 value;
};

class MoveOnlyKey {
 public:
  MoveOnlyKey(S32 value) : value(value) {}
  MoveOnlyKey(const MoveOnlyKey&) = delete;
  MoveOnlyKey(MoveOnlyKey&& source) : value(source.value) { source.value = 0; }

  constexpr auto hash() const -> U64 { return Hash(value).get_value(); }
  constexpr auto operator==(const MoveOnlyKey& rhs) const -> Bool {
    return value == rhs.value;
  }

 private:
  S32 value;
};

template <typename map_type>
static constexpr Bool supports_copy_assignment =
    requires(map_type& destination, const map_type& source) {
      destination = source;
    };

using MoveOnlyValueMap = Thread::Map<S32, MoveOnlyValue>;
static_assert(!__is_constructible(MoveOnlyValueMap, const MoveOnlyValueMap&));
static_assert(!supports_copy_assignment<MoveOnlyValueMap>);
static_assert(__is_constructible(MoveOnlyValueMap, MoveOnlyValueMap&&));

static_assert(!supports_find_or_create<MoveOnlyKey, S32>);
static_assert(!supports_insert<MoveOnlyKey, S32>);
static_assert(supports_insert<S32, MoveOnlyValue>);
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

VALIDATION_TEST(ThreadMap, find_and_visit) {
  Thread::Map<S32, S32> int_map;
  int_map.insert(1, 2);
  int_map.insert(2, 3);
  int_map.insert(4, 5);

  static_assert(__is_same(decltype(int_map.find(2)), Option<S32&>));

  auto found = int_map.find(2);
  ASSERT(found);
  EXPECT_EQ(*found, 3);
  EXPECT(!int_map.find(8));

  const auto& const_map = int_map;
  static_assert(__is_same(decltype(const_map.find(4)), Option<const S32&>));
  auto const_found = const_map.find(4);
  ASSERT(const_found);
  EXPECT_EQ(*const_found, 5);
  EXPECT(!const_map.find(8));

  Count count = 0;
  S32 key_sum = 0;
  S32 value_sum = 0;
  int_map.visit([&](const S32& key, S32& value) {
    count++;
    key_sum += key;
    value_sum += value;
    value++;
  });

  EXPECT_EQ(count, 3);
  EXPECT_EQ(key_sum, 7);
  EXPECT_EQ(value_sum, 10);
  EXPECT_EQ(*int_map[2], 4);
  EXPECT(!int_map[8]);

  count = 0;
  const_map.visit([&](const S32&, const S32&) { count++; });
  EXPECT_EQ(count, 3);
}

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

VALIDATION_TEST(ThreadMap, find_or_create) {
  Thread::Map<S32, S32> empty_map;

  for (Count i = 0; i < 10; i++) {
    empty_map.find_or_create(i);
  }

  EXPECT_EQ(empty_map.get_size(), 10);
  for (Count i = 0; i < 10; i++) {
    EXPECT(empty_map.contains(i));
  }
}

VALIDATION_TEST(ThreadMap, duplicate_keys) {
  Thread::Map<S32, S32> int_map;
  static_assert(
      __is_same(decltype(int_map.insert(S32(1), S32(2))), Option<S32&>));
  auto inserted = int_map.insert(1, 2);
  ASSERT(inserted);
  EXPECT_EQ(*inserted, 2);
  EXPECT(!int_map.insert(1, 4));

  EXPECT_EQ(int_map.get_size(), 1);
  ASSERT_EQ(*int_map[1], 2);

  EXPECT_EQ(*int_map.assign(1, 4), 4);
  EXPECT_EQ(*int_map[1], 4);

  EXPECT(int_map.assign(1, 8));
  EXPECT(!int_map.assign(4, 8));
  ASSERT_EQ(*int_map[1], 8);

  EXPECT(int_map.insert(2, 8));
  EXPECT_EQ(*int_map[2], 8);
}

VALIDATION_TEST(ThreadMap, logical_capacity) {
  Thread::Map<S32, S32> values;
  EXPECT_EQ(values.get_capacity(), Count(0));

  values.ensure_capacity(1000);

  const Count capacity = values.get_capacity();
  EXPECT(capacity >= 1000);

  values.ensure_capacity(capacity);
  EXPECT_EQ(values.get_capacity(), capacity);

  for (Count key = 0; key < capacity; key++) {
    EXPECT(values.insert(key, key));
  }

  EXPECT_EQ(values.get_capacity(), capacity);
  EXPECT(values.insert(capacity, capacity));
  EXPECT(values.get_capacity() > capacity);
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
  Thread::Map<S32, MoveOnlyValue> values;

  EXPECT(values.insert(1, MoveOnlyValue(4)));
  EXPECT_EQ((*values.assign(1, MoveOnlyValue(8))).get_value(), 8);

  auto assigned = values.assign(1, MoveOnlyValue(16));
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

VALIDATION_TEST(ThreadMap, clear_and_reset) {
  Thread::Map<S32, S32> values(32);
  const Count capacity = values.get_capacity();

  values.insert(1, 2);

  values.clear();
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), capacity);

  values.reset();
  values.reset();
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), Count(0));
}

VALIDATION_TEST(ThreadMap, empty_assignment_resets_capacity) {
  Thread::Map<S32, S32> empty;
  Thread::Map<S32, S32> values(32);
  values.insert(1, 2);

  values = empty;

  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), Count(0));
}

VALIDATION_TEST(ThreadMap, remove) {
  Thread::Map<S32, S32> int_map;

  int_map.ensure_capacity(1000);
  for (Count i = 0; i < 100; i++) {
    int_map.insert(i, i + 2);
  }

  EXPECT(int_map.remove(50));
  EXPECT(!int_map.remove(50));
  EXPECT(!int_map.contains(50));
  EXPECT_EQ(int_map.get_size(), Count(99));
  for (Count i = 0; i < 100; i++) {
    if (i != 50) {
      ASSERT_EQ(*int_map[i], i + 2);
    }
  }
}

VALIDATION_TEST(ThreadMap, sentinel_reuse) {
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

  Thread::Map<CollisionKey, Count> values(32);
  for (Count value = 0; value < 24; value++) {
    EXPECT(values.insert(CollisionKey(value), value));
  }

  const Count capacity = values.get_capacity();
  EXPECT(values.remove(CollisionKey(8)));
  EXPECT(values.contains(CollisionKey(16)));
  EXPECT(!values.insert(CollisionKey(16), 32));
  EXPECT(values.insert(CollisionKey(24), 24));
  EXPECT_EQ(values.get_capacity(), capacity);
  EXPECT_EQ(*values[CollisionKey(24)], Count(24));
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
  Count construct_count = 0;
  Count destruct_count = 0;

  {
    Thread::Map<Hashable, S32> custom_map;
    for (Count i = 0; i < 100; i++) {
      custom_map.insert(Hashable(i, construct_count, destruct_count), i);
    }

    EXPECT_EQ(custom_map.get_size(), 100);
    for (Count i = 0; i < 100; i++) {
      ASSERT_EQ(*custom_map[Hashable(i, construct_count, destruct_count)], i);
    }
  }

  EXPECT_EQ(construct_count, destruct_count);
  EXPECT_EQ(default_construct_count, default_destruct_count);
}

VALIDATION_TEST(ThreadMap, value_construction) {
  Count construct_count = 0;
  Count destruct_count = 0;

  {
    Thread::Map<S32, Hashable> custom_map;
    for (Count i = 0; i < 100; i++) {
      custom_map.insert(i, Hashable(i, construct_count, destruct_count));
    }

    EXPECT_EQ(custom_map.get_size(), 100);
    for (Count i = 0; i < 100; i++) {
      ASSERT(*custom_map[i] == Hashable(i, construct_count, destruct_count));
    }
  }

  EXPECT_EQ(construct_count, destruct_count);
  EXPECT_EQ(default_construct_count, default_destruct_count);
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
