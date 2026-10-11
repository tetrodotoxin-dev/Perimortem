// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/memory/managed/vector.hpp"

using namespace Perimortem::Memory;
using namespace Toolchain::Validation;

static Harness ManagedVector = {
  .name = "Memory::Managed::Vector",
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
