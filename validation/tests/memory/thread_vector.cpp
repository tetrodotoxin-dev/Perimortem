// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/core/static/vector.hpp"
#include "perimortem/memory/allocator/global.hpp"
#include "perimortem/memory/thread/vector.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
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

VALIDATION_TEST(ThreadVector, value_semantics) {
  Thread::Vector<U32> original;
  original.insert(4);
  original.insert(8);

  Thread::Vector<U32> copied(original);
  copied.insert(16);

  EXPECT_EQ(original.get_size(), Count(2));
  EXPECT_EQ(copied.get_size(), Count(3));
  EXPECT(copied.get_data() != original.get_data());

  Thread::Vector<U32> moved;
  moved = Data::take(copied);
  EXPECT_EQ(moved.get_size(), Count(3));
  EXPECT(!copied.get_data());
}

VALIDATION_TEST(ThreadVector, view_materialization) {
  U32 source[] = {4, 8, 16};
  Thread::Vector<U32> values(View::Vector<U32>(source, 3));

  EXPECT_EQ(values.get_size(), Count(3));
  EXPECT_EQ(values.get_data()[0], U32(4));
  EXPECT_EQ(values.get_data()[1], U32(8));
  EXPECT_EQ(values.get_data()[2], U32(16));

  values = values.get_view().slice(1, 2);
  EXPECT_EQ(values.get_size(), Count(2));
  EXPECT_EQ(values.get_data()[0], U32(8));
  EXPECT_EQ(values.get_data()[1], U32(16));
}

VALIDATION_TEST(ThreadVector, concat_alias) {
  Thread::Vector<U32> values;
  values.insert(4);
  values.insert(8);

  values.concat(values.get_view());
  EXPECT_EQ(values.get_size(), Count(4));
  EXPECT_EQ(values.get_data()[0], U32(4));
  EXPECT_EQ(values.get_data()[1], U32(8));
  EXPECT_EQ(values.get_data()[2], U32(4));
  EXPECT_EQ(values.get_data()[3], U32(8));

  while (values.get_size() < values.get_capacity()) {
    values.insert(16);
  }

  const Count previous_size = values.get_size();
  values.concat(values.get_view().slice(0, 2));
  EXPECT_EQ(values.get_size(), previous_size + 2);
  EXPECT_EQ(values.get_data()[previous_size], U32(4));
  EXPECT_EQ(values.get_data()[previous_size + 1], U32(8));
}

VALIDATION_TEST(ThreadVector, empty_copy_assignment) {
  Thread::Vector<U32> empty;
  Thread::Vector<U32> destination(64);
  destination.insert(4);

  destination = empty;

  EXPECT(destination.is_empty());
  EXPECT_EQ(destination.get_capacity(), Count(0));
  EXPECT(!destination.get_data());
}

VALIDATION_TEST(ThreadVector, capacity_reservation) {
  Thread::Vector<U32> values;
  values.insert(4);
  values.insert(8);

  const Count current_capacity = values.get_capacity();
  values.ensure_capacity(current_capacity);
  EXPECT_EQ(values.get_capacity(), current_capacity);

  values.ensure_capacity(current_capacity + 1);
  EXPECT(values.get_capacity() > current_capacity);
  EXPECT_EQ(values.get_size(), Count(2));
  EXPECT_EQ(values.get_data()[0], U32(4));
  EXPECT_EQ(values.get_data()[1], U32(8));
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

VALIDATION_TEST(ThreadVector, resize_initializes_scalars) {
  Thread::Vector<U32> values;
  values.insert(U32(0x11111111));
  values.insert(U32(0xAAAAAAAA));

  const Count capacity = values.get_capacity();
  values.resize(1);
  values.resize(3);

  EXPECT_EQ(values.get_capacity(), capacity);
  EXPECT_EQ(values.get_data()[0], U32(0x11111111));
  EXPECT_EQ(values.get_data()[1], U32(0));
  EXPECT_EQ(values.get_data()[2], U32(0));

  U32* allocation = values.get_data();
  values.resize(0);
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), capacity);
  EXPECT(values.get_data() == allocation);
}

VALIDATION_TEST(ThreadVector, forgetful_scalars) {
  Thread::Vector<U32> values;
  values.forgetful_resize(100);
  EXPECT_EQ(values.get_size(), Count(100));
  EXPECT(values.get_capacity() >= 100);

  values.get_data()[99] = 42;
  EXPECT_EQ(values.get_data()[99], U32(42));

  const Count capacity = values.get_capacity();
  U32* allocation = values.get_data();
  values.forgetful_resize(capacity - 1);
  EXPECT(values.get_data() == allocation);

  values.forgetful_resize(capacity >> 1);
  EXPECT(values.get_capacity() < capacity);

  values.forgetful_resize(0);
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), Count(0));
  EXPECT(!values.get_data());
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

VALIDATION_TEST(ThreadVector, remove) {
  Thread::Vector<S32> values;
  values.insert(1);
  values.insert(2);
  values.insert(3);
  values.insert(4);

  EXPECT(values.remove(1));
  EXPECT_EQ(values.get_size(), 3);
  EXPECT_EQ(values.get_data()[0], 1);
  EXPECT_EQ(values.get_data()[1], 4);
  EXPECT_EQ(values.get_data()[2], 3);
  EXPECT(!values.contains(2));
  EXPECT(!values.remove(3));

  EXPECT(values.remove(0));
  EXPECT_EQ(values.get_data()[0], 3);
  EXPECT_EQ(values.get_data()[1], 4);
  EXPECT(values.remove(1));
  EXPECT_EQ(values.get_data()[0], 3);
  EXPECT(values.remove(0));
  EXPECT_EQ(values.get_size(), Count(0));
  EXPECT(!values.remove(0));
}

VALIDATION_TEST(ThreadVector, remove_stable) {
  Thread::Vector<S32> values;
  values.insert(1);
  values.insert(2);
  values.insert(3);
  values.insert(4);

  EXPECT(values.remove_stable(1));
  EXPECT_EQ(values.get_size(), 3);
  EXPECT_EQ(values.get_data()[0], 1);
  EXPECT_EQ(values.get_data()[1], 3);
  EXPECT_EQ(values.get_data()[2], 4);
  EXPECT(!values.contains(2));
  EXPECT(!values.remove_stable(3));

  EXPECT(values.remove_stable(0));
  EXPECT_EQ(values.get_data()[0], 3);
  EXPECT_EQ(values.get_data()[1], 4);
  EXPECT(values.remove_stable(1));
  EXPECT_EQ(values.get_data()[0], 3);
  EXPECT(values.remove_stable(0));
  EXPECT_EQ(values.get_size(), Count(0));
  EXPECT(!values.remove_stable(0));
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
    Static::Vector<U32, 4> owners;
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
  Static::Vector<U32, 5> owners;
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

// MoveOnly cannot be copied during growth. Vector must move construct each
// replacement before releasing the old allocation, including across several
// capacity increases before the value is finally removed.
class MoveOnly {
 public:
  // The fixture records one exact destructor count. A U32 reference must not
  // implicitly construct MoveOnly and add an unrelated destruction.
  explicit MoveOnly(U32& releases) : releases(&releases) {}
  MoveOnly(const MoveOnly&) = delete;
  MoveOnly(MoveOnly&& other) { Data::swap(releases, other.releases); }

  ~MoveOnly() {
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
    Thread::Vector<MoveOnly> values;
    for (U32 index = 0; index < 64; ++index) {
      values.insert(MoveOnly(releases[index]));
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

// An insertion or concatenation source may belong to the Vector being grown.
// Give it a real resource so relocation cannot leave a plausible value behind
// and hide a read from a moved source.
class InsertedValue {
 public:
  // Construction acquires one process allocation. A scalar must not implicitly
  // construct InsertedValue and introduce that allocation and destructor.
  explicit InsertedValue(U32 value) : value(allocate(value)) {}
  InsertedValue(const InsertedValue& other)
      : value(other.value ? allocate(*other.value) : nullptr) {
    copies++;
  }

  InsertedValue(InsertedValue&& other) {
    moves++;
    Data::swap(value, other.value);
  }

  ~InsertedValue() { Allocator::Global::release(Data::cast<U8>(value)); }

  static auto reset_counts() -> void {
    copies = 0;
    moves = 0;
  }

  static auto get_copies() -> Count { return copies; }
  static auto get_moves() -> Count { return moves; }

  U32* value = nullptr;

 private:
  // The fixture allocates each pointee through Global so destroying the source
  // releases that pointee. Global keeps compiler allocation expressions
  // unavailable to ordinary test code.
  static auto allocate(U32 value) -> U32* {
    auto allocation = Allocator::Global::allocate(sizeof(U32));
    U32* result = Data::cast<U32>(allocation.get_data());
    *result = value;
    return result;
  }

  inline static Count copies = 0;
  inline static Count moves = 0;
};

VALIDATION_TEST(ThreadVector, insertion_alias) {
  InsertedValue::reset_counts();

  Thread::Vector<InsertedValue> values;
  values.insert(InsertedValue(42));
  while (values.get_size() < values.get_capacity()) {
    values.insert(InsertedValue(7));
  }

  const Count copies_before_insert = InsertedValue::get_copies();
  EXPECT_EQ(*values.insert(values.get_data()[0]).value, U32(42));
  EXPECT_EQ(*values.get_data()[0].value, U32(42));
  EXPECT_EQ(InsertedValue::get_copies(), copies_before_insert + 1);

  while (values.get_size() < values.get_capacity()) {
    values.insert(InsertedValue(7));
  }

  const Count copies_before_move_insert = InsertedValue::get_copies();
  EXPECT_EQ(*values.insert(Data::take(values.get_data()[0])).value, U32(42));
  EXPECT(values.get_data()[0].value == nullptr);
  EXPECT_EQ(InsertedValue::get_copies(), copies_before_move_insert);
  EXPECT(InsertedValue::get_moves() > values.get_size());
}

VALIDATION_TEST(ThreadVector, concat_alias_lifetimes) {
  InsertedValue::reset_counts();

  Thread::Vector<InsertedValue> values;
  values.insert(InsertedValue(42));
  while (values.get_size() < values.get_capacity()) {
    values.insert(InsertedValue(7));
  }

  const Count previous_size = values.get_size();
  const Count copies_before_concat = InsertedValue::get_copies();
  values.concat(values.get_view().slice(0, 1));

  EXPECT_EQ(values.get_size(), previous_size + 1);
  EXPECT_EQ(*values.get_data()[0].value, U32(42));
  EXPECT_EQ(*values.get_data()[previous_size].value, U32(42));
  EXPECT_EQ(InsertedValue::get_copies(), copies_before_concat + 1);
}
