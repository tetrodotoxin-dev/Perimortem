// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/memory/managed/vector.hpp"

using namespace Perimortem::Memory;
using namespace Toolchain::Validation;

static_assert(!__is_constructible(Managed::Vector<U32>));
static_assert(__is_constructible(Managed::Vector<U32>, Allocator::Arena&));
template <typename type>
static constexpr bool supports_forgetful_resize =
    requires(Managed::Vector<type>& values) { values.forgetful_resize(1); };
static_assert(!supports_forgetful_resize<U32>);

static Harness ManagedVector = {
  .name = "Managed::Vector",
};

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

  values.emplace(MoveValue(moves));

  EXPECT_EQ(values[0].get_moves(), Count(1));
}

VALIDATION_TEST(ManagedVector, const_access) {
  Allocator::Arena arena;
  Managed::Vector<U32> values(arena);
  values.emplace(U32(42));

  const auto& fixed = values;
  static_assert(__is_same(decltype(fixed.at(0)), const U32&));
  static_assert(__is_same(decltype(fixed[0]), const U32&));
  EXPECT_EQ(fixed[0], U32(42));
}

VALIDATION_TEST(ManagedVector, growth_preserves_old_block) {
  Allocator::Arena arena;
  Managed::Vector<CopyValue> values(arena);
  CopyValue first(4);

  values.insert(first);

  const CopyValue* retained = &values[0];
  const Count initial_capacity = values.get_capacity();
  while (values.get_size() < initial_capacity) {
    values.insert(first);
  }

  values.insert(first);

  EXPECT(retained->location == retained);
  EXPECT_EQ(retained->value, Count(4));
  for (Count index = 0; index < values.get_size(); index++) {
    EXPECT(values[index].location == &values[index]);
  }

  values[0].value = 8;
  EXPECT_EQ(retained->value, Count(4));
  EXPECT_EQ(values[0].value, Count(8));
}

VALIDATION_TEST(ManagedVector, independent_copy) {
  Allocator::Arena arena;
  Managed::Vector<CopyValue> source(arena);
  CopyValue value(4);
  source.insert(value);

  auto copied = source;
  copied[0].value = 8;

  EXPECT_EQ(source[0].value, Count(4));
  EXPECT_EQ(copied[0].value, Count(8));
  EXPECT(copied.get_data() != source.get_data());
  EXPECT(&copied.get_arena() == &source.get_arena());
}

VALIDATION_TEST(ManagedVector, prepend) {
  Allocator::Arena arena;
  Managed::Vector<CopyValue> values(arena);
  CopyValue first(4);
  CopyValue second(8);

  values.insert(first);

  const CopyValue* retained = &values[0];
  values.prepend(second);

  EXPECT_EQ(values.get_size(), Count(2));
  EXPECT_EQ(values[0].value, Count(8));
  EXPECT_EQ(values[1].value, Count(4));
  EXPECT(values[0].location == &values[0]);
  EXPECT(values[1].location == &values[1]);
  EXPECT(retained->location == retained);
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
