// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/storage/vector.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/allocator/bibliotheca.hpp"
#include "perimortem/memory/allocator/global.hpp"
#include "perimortem/memory/compile/object.hpp"
#include "perimortem/memory/compile/vector.hpp"
#include "perimortem/memory/global/vector.hpp"
#include "perimortem/memory/managed/bytes.hpp"
#include "perimortem/memory/managed/vector.hpp"
#include "perimortem/memory/thread/vector.hpp"
#include "perimortem/system/process/worker.hpp"
#include "validation/tests/memory/arena_retained_value.hpp"
#include "validation/tests/memory/global_allocation_value.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;
using namespace Perimortem::Validation;
using namespace Toolchain::Validation;

static_assert(__is_constructible(Thread::Vector<U32>));
static_assert(__is_constructible(Thread::Vector<U32>, View::Vector<U32>));
static_assert(
    sizeof(Thread::Vector<U32>) ==
    Data::align<alignof(Count)>(sizeof(U32*)) + sizeof(Count) * 2);
static_assert([] {
  Thread::Vector<U32> values;
  return values.is_empty() && values.get_capacity() == 0 &&
         values.get_data() == nullptr;
}());

static Harness ThreadVector = {
  .name = "Thread::Vector",
};

// Copying, capacity, views, indexed access, resizing, and reset belong to the
// Vector capability. The factories supply each policy's construction
// requirements so every wrapper runs the same capability contract.
template <typename reserve_type, typename view_type>
static auto check_vector_contract(
    reserve_type reserve,
    view_type from_view,
    Test::TestResult& result) -> void {
  auto empty = reserve(0);
  EXPECT(empty.is_empty());
  EXPECT_EQ(empty.get_capacity(), Count(0));
  EXPECT(!empty.get_data());

  auto reserved = reserve(64);
  EXPECT(reserved.is_empty());
  EXPECT(reserved.get_capacity() >= 64);

  U32 source[] = {4, 8, 16};
  auto values = from_view(View::Vector<U32>(source));
  EXPECT_EQ(values.get_size(), Count(3));
  EXPECT_EQ(values.get_data()[0], U32(4));
  EXPECT_EQ(values.get_data()[1], U32(8));
  EXPECT_EQ(values.get_data()[2], U32(16));

  View::Vector<U32> converted_view = values;
  Access::Vector<U32> converted_access = values;
  EXPECT(converted_view.get_data() == values.get_data());
  EXPECT_EQ(converted_view.get_size(), values.get_size());
  EXPECT(converted_access.get_data() == values.get_data());
  EXPECT_EQ(converted_access.get_size(), values.get_size());

  values = values.get_view().slice(1, 2);
  EXPECT_EQ(values.get_size(), Count(2));
  EXPECT_EQ(values.get_data()[0], U32(8));
  EXPECT_EQ(values.get_data()[1], U32(16));

  auto copied = values;
  copied.insert(32);
  EXPECT_EQ(values.get_size(), Count(2));
  EXPECT_EQ(copied.get_size(), Count(3));
  EXPECT(copied.get_data() != values.get_data());

  auto concatenated = from_view(View::Vector<U32>(source));
  U32* initial_concat_allocation = concatenated.get_data();
  concatenated.concat(concatenated.get_view());
  EXPECT_EQ(concatenated.get_size(), Count(6));
  EXPECT_EQ(concatenated.get_data()[3], U32(4));
  EXPECT_EQ(concatenated.get_data()[4], U32(8));
  EXPECT_EQ(concatenated.get_data()[5], U32(16));
  EXPECT(concatenated.get_data() == initial_concat_allocation);

  while (concatenated.get_size() < concatenated.get_capacity()) {
    concatenated.insert(32);
  }

  const Count concatenated_size = concatenated.get_size();
  U32* concatenated_allocation = concatenated.get_data();
  concatenated.concat(concatenated.get_view().slice(0, 3));
  EXPECT_EQ(concatenated.get_size(), concatenated_size + 3);
  EXPECT_EQ(concatenated.get_data()[concatenated_size], U32(4));
  EXPECT_EQ(concatenated.get_data()[concatenated_size + 1], U32(8));
  EXPECT_EQ(concatenated.get_data()[concatenated_size + 2], U32(16));
  EXPECT(concatenated.get_data() != concatenated_allocation);

  auto unordered = from_view(View::Vector<U32>(source));
  U32* unordered_prefix = &unordered.get_data()[0];
  EXPECT(unordered.remove(1));
  EXPECT_EQ(unordered.get_size(), Count(2));
  EXPECT_EQ(unordered.get_data()[0], U32(4));
  EXPECT_EQ(unordered.get_data()[1], U32(16));
  EXPECT(&unordered.get_data()[0] == unordered_prefix);
  EXPECT(!unordered.remove(2));
  EXPECT(unordered.remove(1));
  EXPECT_EQ(unordered.get_size(), Count(1));
  EXPECT_EQ(unordered.get_data()[0], U32(4));

  auto ordered = from_view(View::Vector<U32>(source));
  U32* ordered_prefix = &ordered.get_data()[0];
  EXPECT(ordered.remove_stable(1));
  EXPECT_EQ(ordered.get_size(), Count(2));
  EXPECT_EQ(ordered.get_data()[0], U32(4));
  EXPECT_EQ(ordered.get_data()[1], U32(16));
  EXPECT(&ordered.get_data()[0] == ordered_prefix);
  EXPECT(!ordered.remove_stable(2));

  reserved.insert(4);
  reserved = empty;
  EXPECT(reserved.is_empty());
  EXPECT_EQ(reserved.get_capacity(), Count(0));
  EXPECT(!reserved.get_data());

  const Count capacity = values.get_capacity();
  values.ensure_capacity(capacity);
  EXPECT_EQ(values.get_capacity(), capacity);
  values.ensure_capacity(capacity + 1);
  EXPECT(values.get_capacity() > capacity);
  EXPECT_EQ(values.get_size(), Count(2));
  EXPECT_EQ(values.get_data()[0], U32(8));
  EXPECT_EQ(values.get_data()[1], U32(16));

  values.resize(1);

  const Count retained_capacity = values.get_capacity();
  U32* retained = values.get_data();
  values.resize(3);
  EXPECT_EQ(values.get_capacity(), retained_capacity);
  EXPECT_EQ(values.get_data()[0], U32(8));
  EXPECT_EQ(values.get_data()[1], U32(0));
  EXPECT_EQ(values.get_data()[2], U32(0));

  auto growing_resize = from_view(View::Vector<U32>(source));
  const Count growing_resize_capacity = growing_resize.get_capacity();
  growing_resize.resize(growing_resize_capacity + 2);
  EXPECT(growing_resize.get_capacity() > growing_resize_capacity);
  EXPECT_EQ(growing_resize.get_data()[0], U32(4));
  EXPECT_EQ(growing_resize.get_data()[1], U32(8));
  EXPECT_EQ(growing_resize.get_data()[2], U32(16));
  for (Count index = 3; index < growing_resize.get_size(); index++) {
    EXPECT_EQ(growing_resize.get_data()[index], U32(0));
  }

  static_assert(__is_same(decltype(values[0]), Storage::Option<U32&>));
  ASSERT(values[0]);
  values[0].visit([](U32& value) { value = 84; }, []() {});

  const auto& fixed = values;
  static_assert(__is_same(decltype(fixed[0]), Storage::Option<const U32&>));
  fixed[0].visit([&](U32 value) { EXPECT_EQ(value, U32(84)); }, []() {});
  EXPECT(!fixed[3]);

  values.resize(0);
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), retained_capacity);
  EXPECT(values.get_data() == retained);

  auto retained_values = from_view(View::Vector<U32>(source));
  const Count retained_values_capacity = retained_values.get_capacity();
  U32* retained_values_allocation = retained_values.get_data();
  retained_values.retain(1, Count(-1));
  EXPECT_EQ(retained_values.get_size(), Count(2));
  EXPECT_EQ(retained_values.get_data()[0], U32(8));
  EXPECT_EQ(retained_values.get_data()[1], U32(16));
  EXPECT_EQ(retained_values.get_capacity(), retained_values_capacity);
  EXPECT(retained_values.get_data() == retained_values_allocation);
  retained_values.retain(retained_values.get_size(), 1);
  EXPECT(retained_values.is_empty());
  EXPECT_EQ(retained_values.get_capacity(), retained_values_capacity);
  EXPECT(retained_values.get_data() == retained_values_allocation);

  auto zero_retained = from_view(View::Vector<U32>(source));
  const Count zero_retained_capacity = zero_retained.get_capacity();
  zero_retained.retain(0, 0);
  EXPECT(zero_retained.is_empty());
  EXPECT_EQ(zero_retained.get_capacity(), zero_retained_capacity);

  auto forgotten = reserve(128);
  const Count forgetful_capacity = forgotten.get_capacity();
  U32* forgetful_allocation = forgotten.get_data();
  forgotten.forgetful_resize(forgetful_capacity - 1);
  EXPECT_EQ(forgotten.get_size(), forgetful_capacity - 1);
  EXPECT(forgotten.get_data() == forgetful_allocation);
  forgotten.get_data()[forgetful_capacity - 2] = 42;
  forgotten.forgetful_resize(forgetful_capacity >> 1);
  EXPECT(forgotten.get_capacity() < forgetful_capacity);
  forgotten.forgetful_resize(0);
  EXPECT(forgotten.is_empty());
  EXPECT_EQ(forgotten.get_capacity(), Count(0));
  EXPECT(!forgotten.get_data());

  auto cleared = from_view(View::Vector<U32>(source));
  const Count cleared_capacity = cleared.get_capacity();
  U32* cleared_allocation = cleared.get_data();
  cleared.clear();
  EXPECT(cleared.is_empty());
  EXPECT_EQ(cleared.get_capacity(), cleared_capacity);
  EXPECT(cleared.get_data() == cleared_allocation);
  cleared.insert(64);
  EXPECT_EQ(cleared.get_data()[0], U32(64));

  values.reset();
  values.reset();
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), Count(0));
  EXPECT(!values.get_data());
}

template <typename type>
static constexpr bool supports_forgetful_resize =
    requires(Thread::Vector<type>& values) { values.forgetful_resize(1); };

template <typename type>
static constexpr bool supports_resize =
    requires(Thread::Vector<type>& values) { values.resize(1); };

template <typename type>
static constexpr bool supports_retain =
    requires(Thread::Vector<type>& values) { values.retain(0, 1); };

// Ordinary resize supports values with nontrivial construction and destruction,
// while forgetful resize skips both operations. The checks below also cover a
// deleted constructor.
class ConstructedValue {
 public:
  ConstructedValue() : value(42) {}
  U32 value;
};

class DestructedValue {
 public:
  DestructedValue() = default;
  ~DestructedValue() {}
};

class UnconstructibleValue {
 public:
  UnconstructibleValue() = delete;
};

static_assert(supports_forgetful_resize<U32>);
static_assert(!supports_forgetful_resize<ConstructedValue>);
static_assert(!supports_forgetful_resize<DestructedValue>);
static_assert(!supports_forgetful_resize<UnconstructibleValue>);
static_assert(supports_resize<ConstructedValue>);
static_assert(!supports_resize<UnconstructibleValue>);
static_assert(supports_retain<UnconstructibleValue>);

VALIDATION_TEST(ThreadVector, capability_contract) {
  const Count requests = Allocator::Bibliotheca::check_out_requests();
  Thread::Vector<U32> empty;
  EXPECT(empty.is_empty());
  EXPECT_EQ(Allocator::Bibliotheca::check_out_requests(), requests);

  check_vector_contract(
      [](Count capacity) { return Thread::Vector<U32>(capacity); },
      [](View::Vector<U32> values) { return Thread::Vector<U32>(values); },
      result);
}

VALIDATION_TEST(ThreadVector, move_assignment) {
  Thread::Vector<U32> source;
  source.insert(4);

  Thread::Vector<U32> destination;
  destination = Data::take(source);

  EXPECT_EQ(destination.get_size(), Count(1));
  EXPECT(!source.get_data());
}

class ResizeValue {
 public:
  inline static Count instances = 0;

  ResizeValue() { ++instances; }
  ResizeValue(const ResizeValue& rhs) : value(rhs.value) { ++instances; }
  ~ResizeValue() { --instances; }

  U32 value = 42;
};

// Shrinking ends the removed lifetimes. Growing within capacity must begin
// them again just as growth into a new allocation does, preserving survivors.
VALIDATION_TEST(ThreadVector, resize_lifetimes) {
  {
    Thread::Vector<ResizeValue> values;
    values.resize(3);
    EXPECT_EQ(ResizeValue::instances, Count(3));
    values.get_data()[0].value = 7;

    values.resize(1);
    EXPECT_EQ(ResizeValue::instances, Count(1));
    values.resize(3);
    EXPECT_EQ(ResizeValue::instances, Count(3));
    EXPECT_EQ(values.get_data()[1].value, U32(42));

    values.resize(100);
    EXPECT_EQ(ResizeValue::instances, Count(100));
    EXPECT_EQ(values.get_data()[0].value, U32(7));
  }

  EXPECT_EQ(ResizeValue::instances, Count(0));
}

VALIDATION_TEST(ThreadVector, self_move_assignment) {
  Thread::Vector<U32> values;
  values.insert(4);
  values.insert(8);

  auto* allocation = values.get_data();
  const auto capacity = values.get_capacity();
  auto& self = values;
  values = Data::take(self);

  EXPECT(values.get_data() == allocation);
  EXPECT_EQ(values.get_capacity(), capacity);
  EXPECT_EQ(values.get_size(), Count(2));
  EXPECT_EQ(values.get_data()[0], U32(4));
  EXPECT_EQ(values.get_data()[1], U32(8));
}

// Byte copies can preserve the resource count while leaving a pointer into the
// old object. Each constructor establishes the new address so removal has to
// preserve both the surviving values and their internal relationships.
template <bool copyable>
class RelocationValue {
 public:
  RelocationValue(U32 value, U32& owners) : value(value), owners(&owners) {
    ++owners;
  }

  RelocationValue(const RelocationValue& other)
    requires(copyable)
      : value(other.value), owners(other.owners) {
    ++*owners;
  }

  RelocationValue(RelocationValue&& other)
    requires(!copyable)
      : value(other.value) {
    Data::swap(owners, other.owners);
  }

  ~RelocationValue() {
    if (owners) {
      --*owners;
    }
  }

  const U32 value;
  const RelocationValue* const location = this;

 private:
  U32* owners = nullptr;
};

template <bool copyable>
static auto check_removal_lifetimes(Test::TestResult& result) -> void {
  for (Count stable = 0; stable < 2; ++stable) {
    Storage::Vector<U32, 4> owners;
    {
      Thread::Vector<RelocationValue<copyable>> values;
      auto remove = [&](Count index) {
        return stable ? values.remove_stable(index) : values.remove(index);
      };

      for (U32 index = 0; index < owners.get_size(); ++index) {
        RelocationValue<copyable> value(index, owners[index]);
        if constexpr (copyable) {
          values.insert(value);
        } else {
          values.insert(Data::take(value));
        }
      }

      EXPECT(!remove(4));
      EXPECT_EQ(values.get_size(), Count(4));
      for (Count index = 0; index < owners.get_size(); ++index) {
        EXPECT_EQ(owners[index], U32(1));
      }

      // Removing from the middle and then the front exercises both ordering
      // policies while keeping resource counts tied to the original values.
      EXPECT(remove(1));
      EXPECT_EQ(values.get_size(), Count(3));
      EXPECT_EQ(values.get_data()[0].value, U32(0));
      EXPECT_EQ(values.get_data()[1].value, stable ? U32(2) : U32(3));
      EXPECT_EQ(values.get_data()[2].value, stable ? U32(3) : U32(2));
      EXPECT_EQ(owners[1], U32(0));
      for (Count index = 0; index < values.get_size(); ++index) {
        EXPECT(values.get_data()[index].location == &values.get_data()[index]);
        EXPECT_EQ(owners[values.get_data()[index].value], U32(1));
      }

      EXPECT(remove(0));
      EXPECT_EQ(values.get_size(), Count(2));
      EXPECT_EQ(values.get_data()[0].value, U32(2));
      EXPECT_EQ(values.get_data()[1].value, U32(3));
      EXPECT_EQ(owners[0], U32(0));
      for (Count index = 0; index < values.get_size(); ++index) {
        EXPECT(values.get_data()[index].location == &values.get_data()[index]);
        EXPECT_EQ(owners[values.get_data()[index].value], U32(1));
      }

      // The last value has no replacement. Its lifetime still ends exactly
      // once, including when removing it leaves the vector empty.
      EXPECT(remove(1));
      EXPECT_EQ(values.get_size(), Count(1));
      EXPECT_EQ(values.get_data()[0].value, U32(2));
      EXPECT_EQ(owners[3], U32(0));
      EXPECT(remove(0));
      EXPECT_EQ(values.get_size(), Count(0));
      EXPECT(!remove(0));
    }

    for (Count index = 0; index < owners.get_size(); ++index) {
      EXPECT_EQ(owners[index], U32(0));
    }
  }
}

VALIDATION_TEST(ThreadVector, copy_removal_lifetimes) {
  check_removal_lifetimes<true>(result);
}

VALIDATION_TEST(ThreadVector, move_removal_lifetimes) {
  check_removal_lifetimes<false>(result);
}

template <bool copyable>
static auto check_retain_lifetimes(Test::TestResult& result) -> void {
  Storage::Vector<U32, 5> owners;
  {
    Thread::Vector<RelocationValue<copyable>> values;
    for (U32 index = 0; index < owners.get_size(); ++index) {
      RelocationValue<copyable> value(index, owners[index]);
      if constexpr (copyable) {
        values.insert(value);
      } else {
        values.insert(Data::take(value));
      }
    }

    auto* storage = values.get_data();
    const Count capacity = values.get_capacity();
    values.retain(1, 3);

    EXPECT(values.get_data() == storage);
    EXPECT_EQ(values.get_capacity(), capacity);
    EXPECT_EQ(values.get_size(), Count(3));
    for (Count index = 0; index < values.get_size(); index++) {
      EXPECT_EQ(values.get_data()[index].value, U32(index + 1));
      EXPECT(values.get_data()[index].location == &values.get_data()[index]);
      EXPECT_EQ(owners[index + 1], U32(1));
    }

    EXPECT_EQ(owners[0], U32(0));
    EXPECT_EQ(owners[4], U32(0));

    // A count beyond the available suffix clips to the remaining values.
    // The operation still reuses the allocation and retires the skipped value.
    values.retain(1, Count(-1));
    EXPECT(values.get_data() == storage);
    EXPECT_EQ(values.get_capacity(), capacity);
    EXPECT_EQ(values.get_size(), Count(2));
    EXPECT_EQ(values.get_data()[0].value, U32(2));
    EXPECT_EQ(values.get_data()[1].value, U32(3));
    EXPECT_EQ(owners[1], U32(0));
    EXPECT_EQ(owners[2], U32(1));
    EXPECT_EQ(owners[3], U32(1));
  }

  for (Count index = 0; index < owners.get_size(); index++) {
    EXPECT_EQ(owners[index], U32(0));
  }
}

VALIDATION_TEST(ThreadVector, copy_retain_lifetimes) {
  check_retain_lifetimes<true>(result);
}

VALIDATION_TEST(ThreadVector, move_retain_lifetimes) {
  check_retain_lifetimes<false>(result);
}

// MoveOnlyReleaseValue cannot be copied during growth. Vector must move
// construct each replacement before releasing the old allocation, including
// across several capacity increases before the value is finally removed.
class MoveOnlyReleaseValue {
 public:
  // The fixture records one exact destructor count. A U32 reference must not
  // implicitly construct MoveOnlyReleaseValue and add an unrelated destruction.
  explicit MoveOnlyReleaseValue(U32& releases) : releases(&releases) {}
  MoveOnlyReleaseValue(const MoveOnlyReleaseValue&) = delete;
  MoveOnlyReleaseValue(MoveOnlyReleaseValue&& other) {
    Data::swap(releases, other.releases);
  }

  ~MoveOnlyReleaseValue() {
    if (releases) {
      ++*releases;
    }
  }

 private:
  U32* releases = nullptr;
};

VALIDATION_TEST(ThreadVector, move_only) {
  U32 releases[64] = {};
  {
    Thread::Vector<MoveOnlyReleaseValue> values;
    for (U32 index = 0; index < 64; ++index) {
      values.insert(MoveOnlyReleaseValue(releases[index]));
    }

    values.insert(Data::take(values.get_data()[0]));
    for (U32 count : releases) {
      EXPECT_EQ(count, U32(0));
    }
  }

  for (U32 count : releases) {
    EXPECT_EQ(count, U32(1));
  }
}

VALIDATION_TEST(ThreadVector, insertion_alias) {
  GlobalAllocationValue::Counts counts;

  Thread::Vector<GlobalAllocationValue> values;
  values.insert(GlobalAllocationValue(42, counts));
  while (values.get_size() < values.get_capacity()) {
    values.insert(GlobalAllocationValue(7, counts));
  }

  const Count copies_before_insert = counts.copies;
  EXPECT_EQ(*values.insert(values.get_data()[0]).get_data(), U32(42));
  EXPECT_EQ(*values.get_data()[0].get_data(), U32(42));
  EXPECT_EQ(counts.copies, copies_before_insert + 1);

  while (values.get_size() < values.get_capacity()) {
    values.insert(GlobalAllocationValue(7, counts));
  }

  const Count copies_before_move_insert = counts.copies;
  EXPECT_EQ(
      *values.insert(Data::take(values.get_data()[0])).get_data(), U32(42));
  EXPECT(values.get_data()[0].get_data() == nullptr);
  EXPECT_EQ(counts.copies, copies_before_move_insert);
  EXPECT(counts.moves > values.get_size());
}

VALIDATION_TEST(ThreadVector, concat_alias_lifetimes) {
  GlobalAllocationValue::Counts counts;

  Thread::Vector<GlobalAllocationValue> values;
  values.insert(GlobalAllocationValue(42, counts));
  while (values.get_size() < values.get_capacity()) {
    values.insert(GlobalAllocationValue(7, counts));
  }

  const Count previous_size = values.get_size();
  const Count copies_before_concat = counts.copies;
  values.concat(values.get_view().slice(0, 1));

  EXPECT_EQ(values.get_size(), previous_size + 1);
  EXPECT_EQ(*values.get_data()[0].get_data(), U32(42));
  EXPECT_EQ(*values.get_data()[previous_size].get_data(), U32(42));
  EXPECT_EQ(counts.copies, copies_before_concat + 1);
}

static_assert(__is_constructible(Global::Vector<U32>));
static_assert(__is_constructible(Global::Vector<U32>, View::Vector<U32>));
template <typename type>
static constexpr bool supports_global_forgetful_resize =
    requires(Global::Vector<type>& values) { values.forgetful_resize(1); };
static_assert(supports_global_forgetful_resize<U32>);
static_assert(
    sizeof(Global::Vector<U32>) ==
    Data::align<alignof(Count)>(sizeof(U32*)) + sizeof(Count) * 2);
static_assert([] {
  Global::Vector<U32> values;
  return values.is_empty() && values.get_capacity() == 0 &&
         values.get_data() == nullptr;
}());

static Harness GlobalVector = {
  .name = "Global::Vector",
};

static Global::Vector<U32> transferred_values;
static GlobalAllocationValue::Counts global_vector_counts;
static Global::Vector<GlobalAllocationValue> transferred_resources;

static auto create_global_vector(View::Bytes) -> void {
  Global::Vector<U32> created;
  created.insert(4);
  created.insert(8);
  transferred_values = Data::take(created);
}

static auto create_global_resources(View::Bytes) -> void {
  Global::Vector<GlobalAllocationValue> created;
  for (U32 value = 0; value < 128; value++) {
    created.insert(GlobalAllocationValue(value, global_vector_counts));
  }

  transferred_resources = Data::take(created);
}

VALIDATION_TEST(GlobalVector, capability_contract) {
  check_vector_contract(
      [](Count capacity) { return Global::Vector<U32>(capacity); },
      [](View::Vector<U32> values) { return Global::Vector<U32>(values); },
      result);
}

VALIDATION_TEST(GlobalVector, cross_thread_transfer) {
  auto worker = Process::Worker::start(
      "global.vector"_view, create_global_vector, View::Bytes());
  worker.join();

  EXPECT_EQ(transferred_values.get_size(), Count(2));
  EXPECT_EQ(transferred_values.get_data()[0], U32(4));
  EXPECT_EQ(transferred_values.get_data()[1], U32(8));
  transferred_values.insert(16);
  EXPECT_EQ(transferred_values.get_data()[2], U32(16));

  // Main thread release proves that the process heap block can be released by a
  // thread other than the worker that populated the Vector.
  transferred_values.reset();
  EXPECT(!transferred_values.get_data());
}

// Reallocation consumes the old process heap objects rather than copying them.
// Releasing the completed Vector on this thread proves that both its value
// resources and backing allocation can cross the worker boundary.
VALIDATION_TEST(GlobalVector, resource_cross_thread_reallocation) {
  global_vector_counts = {};

  auto worker = Process::Worker::start(
      "global.resources"_view, create_global_resources, View::Bytes());
  worker.join();

  EXPECT_EQ(transferred_resources.get_size(), Count(128));
  EXPECT_EQ(global_vector_counts.copies, Count(0));
  EXPECT(global_vector_counts.moves > transferred_resources.get_size());
  for (Count index = 0; index < transferred_resources.get_size(); index++) {
    EXPECT_EQ(transferred_resources.get_data()[index].get_value(), U32(index));
    EXPECT(transferred_resources.get_data()[index].has_valid_location());
  }

  EXPECT(transferred_resources.remove(0));
  EXPECT(transferred_resources.remove_stable(0));
  EXPECT_EQ(global_vector_counts.copies, Count(0));

  transferred_resources.reset();
  EXPECT_EQ(global_vector_counts.releases, Count(128));
}

static_assert(
    __is_constructible(Managed::Vector<U32>, Allocator::Arena&, Count));
static_assert(__is_constructible(
    Managed::Vector<U32>,
    Allocator::Arena&,
    View::Vector<U32>));
template <typename type>
static constexpr bool supports_managed_forgetful_resize =
    requires(Managed::Vector<type>& values) { values.forgetful_resize(1); };
static_assert(supports_managed_forgetful_resize<U32>);

static Harness ManagedVector = {
  .name = "Managed::Vector",
};

VALIDATION_TEST(ManagedVector, capability_contract) {
  Allocator::Arena arena;
  const Count requests = Allocator::Bibliotheca::check_out_requests();
  Managed::Vector<U32> empty(arena);
  EXPECT(empty.is_empty());
  EXPECT_EQ(Allocator::Bibliotheca::check_out_requests(), requests);

  check_vector_contract(
      [&](Count capacity) { return Managed::Vector<U32>(arena, capacity); },
      [&](View::Vector<U32> values) {
        return Managed::Vector<U32>(arena, values);
      },

      result);

  Managed::Vector<U32> values(arena);
  EXPECT(&values.get_arena() == &arena);
}

class MoveValue {
 public:
  MoveValue(Count& moves) : moves(&moves) {}
  MoveValue(const MoveValue&) = delete;
  MoveValue(MoveValue&& source) : moves(source.moves) {
    (*moves)++;
    source.moves = nullptr;
  }

  auto get_moves() const -> Count { return *moves; }

 private:
  Count* moves;
};

VALIDATION_TEST(ManagedVector, move) {
  Allocator::Arena arena;
  Managed::Vector<MoveValue> values(arena);
  Count moves = 0;

  values.insert(MoveValue(moves));

  EXPECT_EQ(values.get_data()[0].get_moves(), Count(1));
}

VALIDATION_TEST(ManagedVector, move_construction_transfers_assignment_copies) {
  Allocator::Arena arena;
  Allocator::Arena destination_arena;
  ArenaRetainedValue::Counts counts;
  Managed::Vector<ArenaRetainedValue> source(arena);
  ArenaRetainedValue first(4, counts);
  source.insert(first);

  const ArenaRetainedValue* retained = &source.get_data()[0];
  Managed::Vector<ArenaRetainedValue> moved(Data::take(source));

  EXPECT(source.is_empty());
  EXPECT(&moved.get_data()[0] == retained);

  Managed::Vector<ArenaRetainedValue> assigned(destination_arena);
  assigned = Data::take(moved);
  EXPECT(!moved.is_empty());
  EXPECT(&assigned.get_arena() == &destination_arena);
  EXPECT(&assigned.get_data()[0] != retained);
  EXPECT_EQ(assigned.get_data()[0].get_value(), Count(4));
}

VALIDATION_TEST(ManagedVector, growth_preserves_old_block) {
  Allocator::Arena arena;
  ArenaRetainedValue::Counts counts;
  Managed::Vector<ArenaRetainedValue> values(arena);
  ArenaRetainedValue first(4, counts);

  values.insert(first);

  const ArenaRetainedValue* retained = &values.get_data()[0];
  const Count initial_capacity = values.get_capacity();
  while (values.get_size() < initial_capacity) {
    values.insert(first);
  }

  values.insert(first);

  EXPECT(retained->has_valid_location());
  EXPECT_EQ(retained->get_value(), Count(4));
  for (Count index = 0; index < values.get_size(); index++) {
    EXPECT(values.get_data()[index].has_valid_location());
  }

  values.get_data()[0].get_value() = 8;
  EXPECT_EQ(retained->get_value(), Count(4));
  EXPECT_EQ(values.get_data()[0].get_value(), Count(8));
}

VALIDATION_TEST(ManagedVector, independent_copy) {
  Allocator::Arena arena;
  ArenaRetainedValue::Counts counts;
  Managed::Vector<ArenaRetainedValue> source(arena);
  ArenaRetainedValue value(4, counts);
  source.insert(value);

  auto copied = source;
  copied.get_data()[0].get_value() = 8;

  EXPECT_EQ(source.get_data()[0].get_value(), Count(4));
  EXPECT_EQ(copied.get_data()[0].get_value(), Count(8));
  EXPECT(copied.get_data() != source.get_data());
  EXPECT(&copied.get_arena() == &source.get_arena());
}

VALIDATION_TEST(ManagedVector, cross_arena_copy_assignment) {
  Allocator::Arena source_arena;
  Allocator::Arena destination_arena;
  ArenaRetainedValue::Counts counts;
  Managed::Vector<ArenaRetainedValue> source(source_arena);
  Managed::Vector<ArenaRetainedValue> destination(destination_arena);
  ArenaRetainedValue value(4, counts);
  source.insert(value);
  destination.insert(ArenaRetainedValue(8, counts));

  destination = source;

  EXPECT(&destination.get_arena() == &destination_arena);
  EXPECT(destination.get_data() != source.get_data());
  EXPECT_EQ(destination.get_data()[0].get_value(), Count(4));
  source.get_data()[0].get_value() = 16;
  EXPECT_EQ(destination.get_data()[0].get_value(), Count(4));
}

VALIDATION_TEST(ManagedVector, nested_values_use_destination_arena) {
  Allocator::Arena source_arena;
  Allocator::Arena destination_arena;
  Managed::Vector<Managed::Bytes> source(source_arena);
  source.insert(Managed::Bytes(source_arena, "nested"_view));

  Managed::Vector<Managed::Bytes> destination(destination_arena);
  destination = source;

  ASSERT_EQ(destination.get_size(), Count(1));
  EXPECT(&destination.get_data()[0].get_arena() == &destination_arena);

  source_arena.reset();
  EXPECT_EQ(destination.get_data()[0].get_view(), "nested"_view);
}

VALIDATION_TEST(ManagedVector, nested_insert_uses_destination_arena) {
  Allocator::Arena source_arena;
  Allocator::Arena destination_arena;
  Managed::Bytes source(source_arena, "nested"_view);
  Managed::Vector<Managed::Bytes> destination(destination_arena);

  destination.insert(source);
  EXPECT(&destination.get_data()[0].get_arena() == &destination_arena);

  source_arena.reset();
  EXPECT_EQ(destination.get_data()[0].get_view(), "nested"_view);
}

VALIDATION_TEST(ManagedVector, skips_destructors) {
  ArenaRetainedValue::Counts counts;
  {
    Allocator::Arena arena;
    Managed::Vector<ArenaRetainedValue> values(arena);
    {
      ArenaRetainedValue value(0, counts);
      values.insert(value);
      values.insert(value);
      values.insert(value);
      EXPECT(values.remove(0));
      EXPECT(values.remove_stable(0));
      values.retain(0, 1);
      values.clear();
      values.insert(value);
      values.reset();
      EXPECT_EQ(counts.destructions, Count(0));
    }

    EXPECT_EQ(counts.destructions, Count(1));
  }

  EXPECT_EQ(counts.destructions, Count(1));
}

template <typename type>
static constexpr bool supports_compile_forgetful_resize =
    requires(Compile::Vector<type>& values) { values.forgetful_resize(1); };
static_assert(supports_compile_forgetful_resize<S32>);
static_assert([] {
  Compile::Vector<S32> values;
  values.insert(1);
  values.forgetful_resize(0);
  return values.is_empty() && !values.get_data() && !values.get_capacity();
}());

// Growth may move Object records but must leave their separate pointee
// allocations intact. Schema compilation retains exactly this kind of
// reference.
static_assert(
    [] {
      Compile::Vector<Compile::Object<S32>> values;

      const S32* retained = values.insert(42).get_data();
      for (S32 i = 0; i < 16; ++i) {
        values.insert(i);
      }

      return values.get_data()[0].get_data() == retained && *retained == 42;
    }(),

    "Vector growth moves its values without duplicating their ownership");

// Removal must leave a live, reusable array slot because delete[] will later
// destroy every capacity slot, including those outside the current size.
static_assert(
    [] {
      Compile::Vector<Compile::Object<S32>> values;
      values.insert(10);
      values.insert(20);

      const S32* retained = values.insert(30).get_data();
      if (!values.remove(1) || values.get_data()[1].get_data() != retained) {
        return false;
      }

      values.insert(40);
      if (!values.remove_stable(0) ||
          values.get_data()[0].get_data() != retained) {
        return false;
      }

      return values.get_size() == 2 && *values.get_data()[0].get_data() == 30 &&
             *values.get_data()[1].get_data() == 40 && !values.remove(2) &&
             !values.remove_stable(2);
    }(),

    "Both removals preserve survivors and permit reuse without double "
    "destruction");

// A requested size can exceed one doubling. Shrinking and regrowing inside
// retained capacity must release old values and expose default new slots.
static_assert(
    [] {
      Compile::Vector<Compile::Object<S32>> values;
      values.insert(42);

      const auto* retained = values.get_data()[0].get_data();
      values.resize(100);

      values.get_data()[99] = Compile::Object<S32>(7);

      auto* storage = values.get_data();
      values.resize(1);
      values.resize(100);

      return values.get_capacity() >= 100 && values.get_data() == storage &&
             values.get_data()[0].get_data() == retained &&
             !values.get_data()[99].get_data();
    }(),

    "Resize covers large requests and does not resurrect truncated values");

// Retaining a subrange reuses the allocation and clips the requested count to
// the available suffix. Every capacity slot remains valid for final deletion.
static_assert(
    [] {
      Compile::Vector<S32> values;
      values.insert(10);
      values.insert(20);
      values.insert(30);
      values.insert(40);

      auto* storage = values.get_data();
      const Count capacity = values.get_capacity();
      values.retain(1, Count(-1));

      return values.get_data() == storage &&
             values.get_capacity() == capacity && values.get_size() == 3 &&
             values.get_data()[0] == 20 && values.get_data()[1] == 30 &&
             values.get_data()[2] == 40;
    }(),

    "Retain clips and reuses Compile storage");

static_assert(
    [] {
      Compile::Vector<Compile::Object<S32>> source;
      source.insert(42);

      auto* retained = source.get_data();
      Compile::Vector<Compile::Object<S32>> moved(Data::take(source));

      auto& self = moved;
      moved = Data::take(self);
      Compile::Vector<Compile::Object<S32>> destination;
      destination.insert(7);
      destination = Data::take(moved);
      source.insert(3);

      return destination.get_data() == retained &&
             *destination.get_data()[0].get_data() == 42 && !moved.get_data() &&
             !moved.get_size() && !moved.get_capacity() &&
             *source.get_data()[0].get_data() == 3;
    }(),

    "Vector moves transfer the buffer and leave reusable empty donors");
