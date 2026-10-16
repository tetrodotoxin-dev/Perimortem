// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/static/union.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/null_terminated.hpp"

using namespace Perimortem::Core;
using namespace Toolchain::Validation;

static Harness CoreUnion = {
  .name = "Core::Static::Union",
};

VALIDATION_TEST(CoreUnion, null) {
  Static::Union<U32, View::Bytes, Bool> value;

  EXPECT(value.is_null());
  EXPECT(!value.find<U32>());
  EXPECT(!value.find<View::Bytes>());
}

VALIDATION_TEST(CoreUnion, alternatives) {
  Static::Union<U32, View::Bytes, Bool> bits(U32(42));
  Static::Union<U32, View::Bytes, Bool> text("open"_view);
  Static::Union<U32, View::Bytes, Bool> flag(True);

  EXPECT_EQ(*bits.find<U32>(), U32(42));
  EXPECT_TEXT(*text.find<View::Bytes>(), "open"_view);
  EXPECT(*flag.find<Bool>());
}

VALIDATION_TEST(CoreUnion, visit) {
  auto read = [](const auto& source) {
    return source.visit(
        []() { return Count(1); }, [](U32 bits) { return Count(bits); },
        [](View::Bytes text) { return text.get_size(); });
  };

  Static::Union<U32, View::Bytes> empty;
  Static::Union<U32, View::Bytes> bits(U32(42));
  Static::Union<U32, View::Bytes> text("visit"_view);
  EXPECT_EQ(read(empty), Count(1));
  EXPECT_EQ(read(bits), Count(42));
  EXPECT_EQ(read(text), Count(5));

  bits.visit(
      []() {}, [](U32& selected) -> void { selected++; }, [](View::Bytes&) {});
  EXPECT_EQ(*bits.find<U32>(), U32(43));
}

VALIDATION_TEST(CoreUnion, copy_move) {
  Static::Union<U32, View::Bytes> first("copy"_view);
  Static::Union<U32, View::Bytes> second(first);
  Static::Union<U32, View::Bytes> third(Data::take(second));
  Static::Union<U32, View::Bytes> copied;
  Static::Union<U32, View::Bytes> moved;

  copied = first;
  moved = Data::take(copied);

  EXPECT_TEXT(*third.find<View::Bytes>(), "copy"_view);
  EXPECT_TEXT(*moved.find<View::Bytes>(), "copy"_view);

  moved = Static::Union<U32, View::Bytes>();
  EXPECT(moved.is_null());
}

class MoveOnlyValue {
 public:
  constexpr MoveOnlyValue(U64 value) : value(value) {}
  MoveOnlyValue(const MoveOnlyValue&) = delete;
  constexpr MoveOnlyValue(MoveOnlyValue&& source) : value(source.value) {
    source.value = 0;
  }

  constexpr auto get() const -> U64 { return value; }

 private:
  U64 value;
};

VALIDATION_TEST(CoreUnion, move_only_alternative) {
  MoveOnlyValue source(42);
  Static::Union<MoveOnlyValue, U64> selected(Data::take(source));
  Static::Union<MoveOnlyValue, U64> moved(Data::take(selected));
  auto found = moved.find<MoveOnlyValue>();

  ASSERT(found);
  EXPECT_EQ((*found).get(), U64(42));
  EXPECT_EQ(source.get(), U64(0));
}

static_assert(__is_constructible(Static::Union<U64>, U64));
static_assert(!__is_convertible_to(U64, Static::Union<U64>));
static_assert(!__is_constructible(Static::Union<U64>, S32));
static_assert(!__is_constructible(Static::Union<U64, S64>, S32));
static_assert(
    __is_constructible(Static::Union<MoveOnlyValue, U64>, MoveOnlyValue&&));
static_assert(!__is_constructible(
    Static::Union<MoveOnlyValue, U64>,
    const MoveOnlyValue&));
static_assert(!__is_constructible(
    Static::Union<MoveOnlyValue, U64>,
    const Static::Union<MoveOnlyValue, U64>&));
static_assert(__is_trivially_destructible(Static::Union<View::Bytes, S64>));
