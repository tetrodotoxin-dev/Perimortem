// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/storage/union.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/null_terminated.hpp"
#include "validation/tests/value.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Validation;
using namespace Toolchain::Validation;

static Harness CoreUnion = {
  .name = "Core::Storage::Union",
};

VALIDATION_TEST(CoreUnion, null) {
  Storage::Union<U32, View::Bytes, Bool> value;

  EXPECT(value.is_null());
  EXPECT(!value.find<U32>());
  EXPECT(!value.find<View::Bytes>());
}

VALIDATION_TEST(CoreUnion, alternatives) {
  Storage::Union<U32, View::Bytes, Bool> bits(U32(42));
  Storage::Union<U32, View::Bytes, Bool> text("open"_view);
  Storage::Union<U32, View::Bytes, Bool> flag(True);

  bits.find<U32>().visit(
      [&](U32 value) { EXPECT_EQ(value, U32(42)); }, []() {});
  text.find<View::Bytes>().visit(
      [&](View::Bytes value) { EXPECT_TEXT(value, "open"_view); }, []() {});
  flag.find<Bool>().visit([&](Bool value) { EXPECT(value); }, []() {});
}

VALIDATION_TEST(CoreUnion, visit) {
  auto read = [](const auto& source) {
    return source.visit(
        []() { return Count(1); }, [](U32 bits) { return Count(bits); },
        [](View::Bytes text) { return text.get_size(); });
  };

  Storage::Union<U32, View::Bytes> empty;
  Storage::Union<U32, View::Bytes> bits(U32(42));
  Storage::Union<U32, View::Bytes> text("visit"_view);
  EXPECT_EQ(read(empty), Count(1));
  EXPECT_EQ(read(bits), Count(42));
  EXPECT_EQ(read(text), Count(5));

  bits.visit(
      []() {}, [](U32& selected) -> void { selected++; }, [](View::Bytes&) {});
  bits.find<U32>().visit(
      [&](U32 value) { EXPECT_EQ(value, U32(43)); }, []() {});
}

VALIDATION_TEST(CoreUnion, copy_move) {
  Storage::Union<U32, View::Bytes> first("copy"_view);
  Storage::Union<U32, View::Bytes> second(first);
  Storage::Union<U32, View::Bytes> third(Data::take(second));
  Storage::Union<U32, View::Bytes> copied;
  Storage::Union<U32, View::Bytes> moved;

  copied = first;
  moved = Data::take(copied);

  third.find<View::Bytes>().visit(
      [&](View::Bytes value) { EXPECT_TEXT(value, "copy"_view); }, []() {});
  moved.find<View::Bytes>().visit(
      [&](View::Bytes value) { EXPECT_TEXT(value, "copy"_view); }, []() {});

  moved = Storage::Union<U32, View::Bytes>();
  EXPECT(moved.is_null());
}

VALIDATION_TEST(CoreUnion, move_only_alternative) {
  MoveOnlyValue<U64> source(42);
  Storage::Union<MoveOnlyValue<U64>, U64> selected(Data::take(source));
  Storage::Union<MoveOnlyValue<U64>, U64> moved(Data::take(selected));
  auto found = moved.find<MoveOnlyValue<U64>>();

  ASSERT(found);
  found.visit(
      [&](const MoveOnlyValue<U64>& value) {
        EXPECT_EQ(value.get_value(), U64(42));
      },

      []() {});
  EXPECT_EQ(source.get_value(), U64(0));
}

static_assert(__is_constructible(Storage::Union<U64>, U64));
static_assert(!__is_convertible_to(U64, Storage::Union<U64>));
static_assert(!__is_constructible(Storage::Union<U64>, S32));
static_assert(!__is_constructible(Storage::Union<U64, S64>, S32));
static_assert(__is_constructible(
    Storage::Union<MoveOnlyValue<U64>, U64>,
    MoveOnlyValue<U64>&&));
static_assert(!__is_constructible(
    Storage::Union<MoveOnlyValue<U64>, U64>,
    const MoveOnlyValue<U64>&));
static_assert(!__is_constructible(
    Storage::Union<MoveOnlyValue<U64>, U64>,
    const Storage::Union<MoveOnlyValue<U64>, U64>&));
static_assert(__is_trivially_destructible(Storage::Union<View::Bytes, S64>));
