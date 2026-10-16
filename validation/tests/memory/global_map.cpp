// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/memory/global/map.hpp"
#include "perimortem/system/process/worker.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;
using namespace Toolchain::Validation;

static_assert(__is_constructible(Global::Map<U32, U32>));
static_assert(!__is_convertible_to(Count, Global::Map<U32, U32>));

static Harness GlobalMap = {
  .name = "Global::Map",
};

class HeapValue {
 public:
  // Construction acquires one process allocation. Explicit syntax keeps that
  // allocation visible at every test call site.
  explicit HeapValue(U32 value) : value(allocate(value)) {}

  HeapValue(const HeapValue& source)
      : value(source.value ? allocate(*source.value) : nullptr) {
    copies++;
  }

  HeapValue(HeapValue&& source) {
    moves++;
    Data::swap(value, source.value);
  }

  auto operator=(const HeapValue&) -> HeapValue& = delete;
  auto operator=(HeapValue&&) -> HeapValue& = delete;

  ~HeapValue() { release(); }

  static auto reset_counts() -> void {
    copies = 0;
    moves = 0;
    releases = 0;
  }

  static auto get_copies() -> Count { return copies; }
  static auto get_moves() -> Count { return moves; }
  static auto get_releases() -> Count { return releases; }

  auto get_value() const -> U32 { return *value; }
  auto has_valid_location() const -> Bool { return location == this; }

 private:
  static auto allocate(U32 source) -> U32* {
    auto allocation = Allocator::Global::allocate(sizeof(U32));
    U32* result = Data::cast<U32>(allocation.get_data());
    *result = source;
    return result;
  }

  auto release() -> void {
    if (value) {
      Allocator::Global::release(Data::cast<U8>(value));
      value = nullptr;
      releases++;
    }
  }

  U32* value = nullptr;
  const HeapValue* const location = this;
  inline static Count copies = 0;
  inline static Count moves = 0;
  inline static Count releases = 0;
};

static Global::Map<U32, HeapValue> transferred_map;

static auto create_global_map(View::Bytes) -> void {
  Global::Map<U32, HeapValue> created;
  for (U32 value = 0; value < 128; value++) {
    created.insert(U32(value), HeapValue(value));
  }

  transferred_map = Data::take(created);
}

VALIDATION_TEST(GlobalMap, value_semantics) {
  Global::Map<U32, U32> original;
  original.insert(4, 8);
  original.insert(8, 16);

  Global::Map<U32, U32> copied(original);
  copied.insert(16, 32);

  EXPECT_EQ(original.get_size(), Count(2));
  EXPECT_EQ(copied.get_size(), Count(3));
  EXPECT(!original.contains(16));
  EXPECT(copied[16]);
  EXPECT_EQ(*copied[16], U32(32));
}

VALIDATION_TEST(GlobalMap, empty_assignment_resets_capacity) {
  Global::Map<U32, U32> empty;
  Global::Map<U32, U32> values(32);
  values.insert(4, 8);

  values = empty;

  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), Count(0));
}

VALIDATION_TEST(GlobalMap, logical_capacity) {
  Global::Map<U32, U32> values;
  values.ensure_capacity(1000);

  const Count capacity = values.get_capacity();
  EXPECT(capacity >= 1000);
  values.ensure_capacity(capacity);
  EXPECT_EQ(values.get_capacity(), capacity);
}

// Rehashing consumes former keys and values in worker allocated process
// storage. Reading and releasing the completed Map on this thread proves that
// its keys, values, and allocation can cross the worker boundary.
VALIDATION_TEST(GlobalMap, cross_thread_reallocation) {
  HeapValue::reset_counts();

  auto worker = Process::Worker::start(
      "global.map"_view, create_global_map, View::Bytes());
  worker.join();

  EXPECT_EQ(transferred_map.get_size(), Count(128));
  EXPECT_EQ(HeapValue::get_copies(), Count(0));
  EXPECT(HeapValue::get_moves() > transferred_map.get_size());
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
  EXPECT_EQ(HeapValue::get_releases(), Count(128));
}
