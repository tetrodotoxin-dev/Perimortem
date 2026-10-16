// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/memory/global/set.hpp"
#include "perimortem/system/process/worker.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;
using namespace Toolchain::Validation;

static_assert(__is_constructible(Global::Set<U32>));
static_assert(!__is_convertible_to(Count, Global::Set<U32>));

static Harness GlobalSet = {
  .name = "Global::Set",
};

class HeapKey {
 public:
  // Construction acquires one process allocation. Explicit syntax keeps that
  // allocation visible at every test call site.
  explicit HeapKey(U32 value) : value(allocate(value)) {}

  HeapKey(const HeapKey& source)
      : value(source.value ? allocate(*source.value) : nullptr) {
    copies++;
  }

  HeapKey(HeapKey&& source) {
    moves++;
    Data::swap(value, source.value);
  }

  auto operator=(const HeapKey&) -> HeapKey& = delete;
  auto operator=(HeapKey&&) -> HeapKey& = delete;

  ~HeapKey() { release(); }

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
  auto hash() const -> U64 { return Hash(*value).get_value(); }
  auto operator==(const HeapKey& rhs) const -> Bool {
    return *value == *rhs.value;
  }

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
  const HeapKey* const location = this;
  inline static Count copies = 0;
  inline static Count moves = 0;
  inline static Count releases = 0;
};

static Global::Set<HeapKey> transferred_set;

static auto create_global_set(View::Bytes) -> void {
  Global::Set<HeapKey> created;
  for (U32 value = 0; value < 1024; value++) {
    created.insert(HeapKey(value));
  }

  transferred_set = Data::take(created);
}

VALIDATION_TEST(GlobalSet, value_semantics) {
  Global::Set<U32> original;
  original.insert(4);
  original.insert(8);

  Global::Set<U32> copied(original);
  copied.insert(16);

  EXPECT_EQ(original.get_size(), Count(2));
  EXPECT_EQ(copied.get_size(), Count(3));
  EXPECT(!original.contains(16));
  EXPECT(copied.contains(16));
}

VALIDATION_TEST(GlobalSet, empty_assignment_resets_capacity) {
  Global::Set<U32> empty;
  Global::Set<U32> values(32);
  values.insert(4);

  values = empty;

  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), Count(0));
}

VALIDATION_TEST(GlobalSet, logical_capacity) {
  Global::Set<U32> values;
  values.ensure_capacity(1000);

  const Count capacity = values.get_capacity();
  EXPECT(capacity >= 1000);
  values.ensure_capacity(capacity);
  EXPECT_EQ(values.get_capacity(), capacity);
}

VALIDATION_TEST(GlobalSet, cross_thread_reallocation) {
  HeapKey::reset_counts();

  auto worker = Process::Worker::start(
      "global.set"_view, create_global_set, View::Bytes());
  worker.join();

  EXPECT_EQ(transferred_set.get_size(), Count(1024));
  EXPECT_EQ(HeapKey::get_copies(), Count(0));
  EXPECT(HeapKey::get_moves() > transferred_set.get_size());

  Count visited = 0;
  transferred_set.visit([&](const HeapKey& value) {
    visited++;
    EXPECT(value.has_valid_location());
    EXPECT(value.get_value() < 1024);
  });

  EXPECT_EQ(visited, Count(1024));

  // Reset releases worker allocated storage from this thread.
  transferred_set.reset();
  EXPECT(transferred_set.is_empty());
  EXPECT_EQ(transferred_set.get_capacity(), Count(0));
  EXPECT_EQ(HeapKey::get_releases(), Count(1024));
}
