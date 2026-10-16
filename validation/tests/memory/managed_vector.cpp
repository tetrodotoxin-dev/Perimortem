// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/core/null_terminated.hpp"
#include "perimortem/memory/managed/bytes.hpp"
#include "perimortem/memory/managed/vector.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Toolchain::Validation;

static_assert(!__is_constructible(Managed::Vector<U32>));
static_assert(__is_constructible(Managed::Vector<U32>, Allocator::Arena&));
static_assert(
    __is_constructible(Managed::Vector<U32>, Allocator::Arena&, Count));
static_assert(__is_constructible(
    Managed::Vector<U32>,
    Allocator::Arena&,
    View::Vector<U32>));
template <typename type>
static constexpr bool supports_forgetful_resize =
    requires(Managed::Vector<type>& values) { values.forgetful_resize(1); };
static_assert(supports_forgetful_resize<U32>);

static Harness ManagedVector = {
  .name = "Managed::Vector",
};

VALIDATION_TEST(ManagedVector, empty_representation) {
  Allocator::Arena arena;
  Managed::Vector<U32> values(arena);

  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), Count(0));
  EXPECT(!values.get_data());

  values.insert(4);
  values.reset();
  values.reset();
  EXPECT(values.is_empty());
  EXPECT_EQ(values.get_capacity(), Count(0));
  EXPECT(!values.get_data());
}

VALIDATION_TEST(ManagedVector, empty_copy_assignment) {
  Allocator::Arena arena;
  Managed::Vector<U32> empty(arena);
  Managed::Vector<U32> destination(arena, 64);
  destination.insert(4);

  destination = empty;

  EXPECT(destination.is_empty());
  EXPECT_EQ(destination.get_capacity(), Count(0));
  EXPECT(!destination.get_data());
}

VALIDATION_TEST(ManagedVector, capacity_and_view_construction) {
  Allocator::Arena arena;
  Managed::Vector<U32> reserved(arena, 64);
  EXPECT(reserved.is_empty());
  EXPECT(reserved.get_capacity() >= 64);

  U32 source[] = {4, 8, 16};
  Managed::Vector<U32> copied(arena, View::Vector<U32>(source, 3));
  EXPECT_EQ(copied.get_size(), Count(3));
  EXPECT_EQ(copied.get_data()[0], U32(4));
  EXPECT_EQ(copied.get_data()[1], U32(8));
  EXPECT_EQ(copied.get_data()[2], U32(16));

  copied = copied.get_view().slice(1, 2);
  EXPECT_EQ(copied.get_size(), Count(2));
  EXPECT_EQ(copied.get_data()[0], U32(8));
  EXPECT_EQ(copied.get_data()[1], U32(16));
  EXPECT(&copied.get_arena() == &arena);
}

VALIDATION_TEST(ManagedVector, resize_initializes_scalars) {
  Allocator::Arena arena;
  Managed::Vector<U32> values(arena);
  values.insert(U32(0x11111111));
  values.insert(U32(0xAAAAAAAA));

  const Count capacity = values.get_capacity();
  values.resize(1);
  values.resize(3);

  EXPECT_EQ(values.get_capacity(), capacity);
  EXPECT_EQ(values.get_data()[0], U32(0x11111111));
  EXPECT_EQ(values.get_data()[1], U32(0));
  EXPECT_EQ(values.get_data()[2], U32(0));
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

class CopyValue {
 public:
  CopyValue(Count value) : value(value) {}
  CopyValue(const CopyValue& source) : value(source.value) {}

  Count value;
  const CopyValue* location = this;
};

class LifetimeValue {
 public:
  LifetimeValue(Count& destructions) : destructions(&destructions) {}
  LifetimeValue(const LifetimeValue& source)
      : destructions(source.destructions) {}
  ~LifetimeValue() { (*destructions)++; }

 private:
  Count* destructions;
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
  Managed::Vector<CopyValue> source(arena);
  CopyValue first(4);
  source.insert(first);

  const CopyValue* retained = &source.get_data()[0];
  Managed::Vector<CopyValue> moved(Data::take(source));

  EXPECT(source.is_empty());
  EXPECT(&moved.get_data()[0] == retained);

  Managed::Vector<CopyValue> assigned(destination_arena);
  assigned = Data::take(moved);
  EXPECT(!moved.is_empty());
  EXPECT(&assigned.get_arena() == &destination_arena);
  EXPECT(&assigned.get_data()[0] != retained);
  EXPECT_EQ(assigned.get_data()[0].value, Count(4));
}

VALIDATION_TEST(ManagedVector, bounded_access) {
  Allocator::Arena arena;
  Managed::Vector<U32> values(arena);
  values.insert(U32(42));

  static_assert(__is_same(decltype(values[0]), Option<U32&>));
  ASSERT(values[0]);
  *values[0] = 84;

  const auto& fixed = values;
  static_assert(__is_same(decltype(fixed[0]), Option<const U32&>));
  EXPECT(fixed[0]);
  EXPECT_EQ(*fixed[0], U32(84));
  EXPECT(!fixed[1]);
}

VALIDATION_TEST(ManagedVector, growth_preserves_old_block) {
  Allocator::Arena arena;
  Managed::Vector<CopyValue> values(arena);
  CopyValue first(4);

  values.insert(first);

  const CopyValue* retained = &values.get_data()[0];
  const Count initial_capacity = values.get_capacity();
  while (values.get_size() < initial_capacity) {
    values.insert(first);
  }

  values.insert(first);

  EXPECT(retained->location == retained);
  EXPECT_EQ(retained->value, Count(4));
  for (Count index = 0; index < values.get_size(); index++) {
    EXPECT(values.get_data()[index].location == &values.get_data()[index]);
  }

  values.get_data()[0].value = 8;
  EXPECT_EQ(retained->value, Count(4));
  EXPECT_EQ(values.get_data()[0].value, Count(8));
}

VALIDATION_TEST(ManagedVector, independent_copy) {
  Allocator::Arena arena;
  Managed::Vector<CopyValue> source(arena);
  CopyValue value(4);
  source.insert(value);

  auto copied = source;
  copied.get_data()[0].value = 8;

  EXPECT_EQ(source.get_data()[0].value, Count(4));
  EXPECT_EQ(copied.get_data()[0].value, Count(8));
  EXPECT(copied.get_data() != source.get_data());
  EXPECT(&copied.get_arena() == &source.get_arena());
}

VALIDATION_TEST(ManagedVector, cross_arena_copy_assignment) {
  Allocator::Arena source_arena;
  Allocator::Arena destination_arena;
  Managed::Vector<CopyValue> source(source_arena);
  Managed::Vector<CopyValue> destination(destination_arena);
  CopyValue value(4);
  source.insert(value);
  destination.insert(CopyValue(8));

  destination = source;

  EXPECT(&destination.get_arena() == &destination_arena);
  EXPECT(destination.get_data() != source.get_data());
  EXPECT_EQ(destination.get_data()[0].value, Count(4));
  source.get_data()[0].value = 16;
  EXPECT_EQ(destination.get_data()[0].value, Count(4));
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
  Count destructions = 0;
  {
    Allocator::Arena arena;
    Managed::Vector<LifetimeValue> values(arena);
    {
      LifetimeValue value(destructions);
      values.insert(value);
      values.insert(value);
      values.insert(value);
      EXPECT(values.remove(0));
      EXPECT(values.remove_stable(0));
      values.retain(0, 1);
      values.clear();
      values.insert(value);
      values.reset();
      EXPECT_EQ(destructions, Count(0));
    }

    EXPECT_EQ(destructions, Count(1));
  }

  EXPECT_EQ(destructions, Count(1));
}
