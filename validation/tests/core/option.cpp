// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/option.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/null_terminated.hpp"

using namespace Perimortem::Core;
using namespace Toolchain::Validation;

static Harness CoreOption = {
  .name = "Core::Option",
};

// Presence is useful in conditions, but it cannot stand in for the selected
// integer. Otherwise assigning an unread Option can quietly store one instead
// of the value its caller meant to extract.
static_assert(!__is_convertible_to(Option<U64>, U64));
static_assert(!__is_convertible_to(Option<U64&>, U64));
static_assert(requires(Option<U64> value) { value ? true : false; });
static_assert(requires(Option<U64&> value) { value ? true : false; });
static_assert(Bool(Option<U64>(U64(0))) == True);
static_assert(Bool(Option<U64>()) == False);
static_assert([] {
  U64 value = 0;
  return Bool(Option<U64&>(value)) == True && Bool(Option<U64&>()) == False;
}());

class ReferenceBase {};
class ReferenceDerived final : public ReferenceBase {};

class MoveOnlyValue {
 public:
  MoveOnlyValue(S32 value) : value(value) {}

  MoveOnlyValue(const MoveOnlyValue&) = delete;
  MoveOnlyValue(MoveOnlyValue&& source) : value(source.value) {
    source.value = 0;
  }

  auto operator=(const MoveOnlyValue&) -> MoveOnlyValue& = delete;
  auto operator=(MoveOnlyValue&&) -> MoveOnlyValue& = delete;

  constexpr auto get() const -> S32 { return value; }

 private:
  S32 value;
};

class TrackedValue {
 public:
  struct Counts {
    Count constructions = 0;
    Count destructions = 0;
    Count copies = 0;
    Count moves = 0;
  };

  TrackedValue(S32 value, Counts& counts) : value(value), counts(counts) {
    counts.constructions++;
  }

  TrackedValue(const TrackedValue& source)
      : value(source.value), counts(source.counts) {
    counts.constructions++;
    counts.copies++;
  }

  TrackedValue(TrackedValue&& source)
      : value(source.value), counts(source.counts) {
    counts.constructions++;
    counts.moves++;
    source.value = 0;
  }

  ~TrackedValue() { counts.destructions++; }

  auto operator=(const TrackedValue&) -> TrackedValue& = delete;
  auto operator=(TrackedValue&&) -> TrackedValue& = delete;

  constexpr auto get() const -> S32 { return value; }

 private:
  S32 value;
  Counts& counts;
};

class StackValue {
 public:
  StackValue(S32 value, Count& destructions)
      : value(value), destructions(destructions) {}

  StackValue(const StackValue& source)
      : value(source.value), destructions(source.destructions) {}
  StackValue(StackValue&& source)
      : value(source.value), destructions(source.destructions) {
    source.moved = True;
  }

  ~StackValue() {
    if (!moved) {
      destructions++;
    }
  }

  auto increment() -> void { value++; }
  auto get() const -> S32 { return value; }

 private:
  S32 value;
  Bool moved = False;
  Count& destructions;
};

static auto create_stack_value(Count& destructions) -> Option<StackValue> {
  StackValue value(41, destructions);
  return Data::take(value);
}

VALIDATION_TEST(CoreOption, visits_none) {
  Option<const S32&> selected;

  Count branch = selected.visit(
      [](const S32&) { return Count(2); }, []() { return Count(1); });

  EXPECT_EQ(branch, 1);
}

VALIDATION_TEST(CoreOption, visits_reference) {
  S32 value = 41;
  Option<S32&> selected(value);

  selected.visit([](S32& found) { found++; }, []() {});

  EXPECT_EQ(value, 42);
}

VALIDATION_TEST(CoreOption, copies_reference) {
  const S32 value = 42;
  Option<const S32&> first(value);
  Option<const S32&> second(first);

  S32 found = second.visit(
      [](const S32& selected) { return selected; }, []() { return S32(0); });

  EXPECT_EQ(found, value);
}

VALIDATION_TEST(CoreOption, copies_value) {
  Option<S32> first(41);
  Option<S32> second(first);

  second.visit([](S32& selected) { selected++; }, []() {});

  S32 first_value = first.visit(
      [](S32 selected) { return selected; }, []() { return S32(0); });
  S32 second_value = second.visit(
      [](S32 selected) { return selected; }, []() { return S32(0); });

  EXPECT_EQ(first_value, 41);
  EXPECT_EQ(second_value, 42);
}

VALIDATION_TEST(CoreOption, owns_stack_value) {
  Count destructions = 0;

  {
    Option<StackValue> selected = create_stack_value(destructions);
    Option<StackValue> moved(Data::take(selected));

    moved.visit([](StackValue& value) { value.increment(); }, []() {});

    const Option<StackValue>& observed = moved;
    S32 found = observed.visit(
        [](const StackValue& value) { return value.get(); },
        []() { return S32(0); });

    EXPECT_EQ(found, 42);
  }

  EXPECT_EQ(destructions, Count(1));
}

VALIDATION_TEST(CoreOption, mutable_and_const_visitors) {
  Count owned_destructions = 0;
  Option<StackValue> owned = create_stack_value(owned_destructions);
  owned.visit([](StackValue& value) { value.increment(); }, []() {});

  const Option<StackValue>& const_owned = owned;
  const S32 owned_value = const_owned.visit(
      [](const StackValue& value) { return value.get(); },
      []() { return S32(0); });
  EXPECT_EQ(owned_value, 42);

  Count reference_destructions = 0;
  StackValue value(40, reference_destructions);
  Option<StackValue&> reference(value);
  reference.visit([](StackValue& selected) { selected.increment(); }, []() {});

  const Option<StackValue&>& const_reference = reference;
  const_reference.visit(
      [](StackValue& selected) { selected.increment(); }, []() {});
  EXPECT_EQ(value.get(), 42);
}

VALIDATION_TEST(CoreOption, accepts_empty) {
  Count destructions = 0;
  Option<StackValue> selected = create_stack_value(destructions);
  selected = {};

  Count branch = selected.visit(
      [](const StackValue&) { return Count(2); }, []() { return Count(1); });

  EXPECT_EQ(branch, Count(1));
  EXPECT_EQ(destructions, Count(1));
}

VALIDATION_TEST(CoreOption, empty_copy_and_move) {
  Option<S32> empty;
  Option<S32> copied(empty);
  Option<S32> moved(Data::take(empty));

  EXPECT_NOT(empty);
  EXPECT_NOT(copied);
  EXPECT_NOT(moved);
}

VALIDATION_TEST(CoreOption, assignment_transitions) {
  TrackedValue::Counts counts;
  {
    TrackedValue first(1, counts);
    TrackedValue second(2, counts);
    Option<TrackedValue> empty;
    Option<TrackedValue> selected(first);
    Option<TrackedValue> replacement(second);

    empty = selected;
    empty.visit(
        [&](const TrackedValue& value) { EXPECT_EQ(value.get(), 1); }, []() {});

    selected = replacement;
    selected.visit(
        [&](const TrackedValue& value) { EXPECT_EQ(value.get(), 2); }, []() {});

    replacement = Option<TrackedValue>();
    EXPECT_NOT(replacement);

    empty = Option<TrackedValue>();
    EXPECT_NOT(empty);

    auto* selected_address = &selected;
    selected = *selected_address;
    selected = Data::take(*selected_address);
    selected.visit(
        [&](const TrackedValue& value) { EXPECT_EQ(value.get(), 2); }, []() {});

    Option<TrackedValue> move_source(first);
    empty = Data::take(move_source);
    EXPECT(move_source);
    empty.visit(
        [&](const TrackedValue& value) { EXPECT_EQ(value.get(), 1); }, []() {});

    Option<TrackedValue> second_move_source(second);
    empty = Data::take(second_move_source);
    EXPECT(second_move_source);
    empty.visit(
        [&](const TrackedValue& value) { EXPECT_EQ(value.get(), 2); }, []() {});
  }

  EXPECT_EQ(counts.constructions, counts.destructions);
}

VALIDATION_TEST(CoreOption, destroys_selected_values) {
  TrackedValue::Counts counts;
  {
    TrackedValue source(7, counts);
    {
      Option<TrackedValue> selected(source);
      EXPECT_EQ(counts.constructions, Count(2));
      EXPECT_EQ(counts.destructions, Count(0));

      selected = Option<TrackedValue>();
      EXPECT_EQ(counts.destructions, Count(1));
    }

    EXPECT_EQ(counts.destructions, Count(1));
  }

  EXPECT_EQ(counts.constructions, Count(2));
  EXPECT_EQ(counts.destructions, Count(2));
}

VALIDATION_TEST(CoreOption, owns_move_only_values) {
  Option<MoveOnlyValue> source(MoveOnlyValue(42));
  Option<MoveOnlyValue> moved(Data::take(source));

  EXPECT(source);
  source.visit(
      [&](const MoveOnlyValue& value) { EXPECT_EQ(value.get(), 0); }, []() {});
  moved.visit(
      [&](const MoveOnlyValue& value) { EXPECT_EQ(value.get(), 42); }, []() {});
}

VALIDATION_TEST(CoreOption, rebinds_references) {
  S32 first = 1;
  S32 second = 2;
  Option<S32&> selected(first);
  Option<S32&> replacement(second);

  selected = replacement;
  selected.visit([](S32& value) { value = 3; }, []() {});

  EXPECT_EQ(first, 1);
  EXPECT_EQ(second, 3);

  selected = {};
  EXPECT_NOT(selected);
}

template <typename type>
static constexpr Bool supports_indirection = requires(type value) { *value; };

template <typename type>
static constexpr Bool supports_arrow =
    requires(type value) { value.operator->(); };

static_assert(__is_trivially_copyable(Option<U64>));
static_assert(__is_trivially_destructible(Option<U64>));
static_assert(__is_trivially_copyable(Option<const S32&>));
static_assert(sizeof(Option<const S32&>) == sizeof(const S32*));
static_assert(!supports_indirection<Option<U64>>);
static_assert(!supports_indirection<Option<U64&>>);
static_assert(!supports_arrow<Option<U64>>);
static_assert(!supports_arrow<Option<U64&>>);
static_assert(__is_constructible(Option<const S32&>, const S32&));
static_assert(!__is_constructible(Option<const S32&>, S32&&));
static_assert(
    !__is_constructible(Option<const ReferenceBase&>, ReferenceDerived&&));
static_assert(__is_constructible(Option<StackValue>, StackValue&&));
static_assert(__is_constructible(Option<StackValue>, Option<StackValue>&&));
static_assert(!__is_constructible(Option<MoveOnlyValue>, const MoveOnlyValue&));
static_assert(__is_constructible(Option<MoveOnlyValue>, MoveOnlyValue&&));
