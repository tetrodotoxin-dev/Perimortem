// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/serialization/version.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/core/view/null_terminated.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Serialization;
using namespace Toolchain::Validation;

static Harness SerializationVersion = {
  .name = "Serialization::Version",
};

static_assert(Version<>::get_component_count() == 2);
static_assert(Version<4>::parse("1.2"_view)
                  .visit(
                      [](const Version<4>& selected) {
                        return selected == Version<4>{{1, 2, 0, 0}};
                      },

                      []() { return False; }));

VALIDATION_TEST(SerializationVersion, component_extent) {
  constexpr Version<> empty;
  constexpr Version<1> single = {{7}};
  constexpr Version<4> extended = {{1, 2}};

  EXPECT((empty == Version<>{{0, 0}}));
  EXPECT_EQ(single.get_components()[0], U16(7));
  EXPECT((extended == Version<4>{{1, 2, 0, 0}}));
  EXPECT_EQ(extended.get_components().get_size(), 4);
}

VALIDATION_TEST(SerializationVersion, comparison) {
  constexpr Version<3> development = {{0, 3, 2}};
  constexpr Version<3> next_patch = {{0, 3, 3}};
  constexpr Version<3> next_minor = {{0, 4, 0}};
  constexpr Version<3> release = {{1, 0, 0}};

  EXPECT(development < next_patch);
  EXPECT(next_patch < next_minor);
  EXPECT(next_minor < release);
  EXPECT(development < release);
  EXPECT(release > development);
  EXPECT(development != release);
}

VALIDATION_TEST(SerializationVersion, parse_zero_fills_missing_components) {
  auto single = Version<4>::parse("1"_view);
  auto pair = Version<4>::parse("1.2"_view);
  auto complete = Version<4>::parse("1.2.3.4"_view);

  ASSERT(single);
  ASSERT(pair);
  ASSERT(complete);

  single.visit(
      [&](const Version<4>& selected) {
        EXPECT((selected == Version<4>{{1, 0, 0, 0}}));
      },

      []() {});
  pair.visit(
      [&](const Version<4>& selected) {
        EXPECT((selected == Version<4>{{1, 2, 0, 0}}));
      },

      []() {});
  complete.visit(
      [&](const Version<4>& selected) {
        EXPECT((selected == Version<4>{{1, 2, 3, 4}}));
      },

      []() {});
}

VALIDATION_TEST(SerializationVersion, parse_stops_before_nonnumeric_suffix) {
  auto separated = Version<3>::parse("1.2.A"_view);
  auto adjacent = Version<3>::parse("1.2beta"_view);

  ASSERT(separated);
  ASSERT(adjacent);

  separated.visit(
      [&](const Version<3>& selected) {
        EXPECT((selected == Version<3>{{1, 2, 0}}));
      },

      []() {});
  adjacent.visit(
      [&](const Version<3>& selected) {
        EXPECT((selected == Version<3>{{1, 2, 0}}));
      },

      []() {});
}

VALIDATION_TEST(SerializationVersion, parse_rejects_narrowing) {
  EXPECT_NOT(Version<1>::parse("1.2"_view));
  EXPECT_NOT(Version<2>::parse("1.2.3"_view));
  EXPECT_NOT(Version<3>::parse("1.2.3.4"_view));
}

VALIDATION_TEST(SerializationVersion, parse_rejects_invalid_components) {
  EXPECT_NOT(Version<>::parse(""_view));
  EXPECT_NOT(Version<>::parse(".1"_view));
  EXPECT_NOT(Version<>::parse("A"_view));
  EXPECT_NOT(Version<>::parse("01.2"_view));
  EXPECT_NOT(Version<>::parse("1.02"_view));
  EXPECT_NOT(Version<>::parse("65536.0"_view));
  EXPECT_NOT(Version<>::parse("0.65536"_view));
}

VALIDATION_TEST(SerializationVersion, serialize_exact_component_extent) {
  auto parsed = Version<4>::parse("1.2"_view);
  Storage::Bytes<Version<4>::get_max_serialized_size()> output;

  ASSERT(parsed);
  parsed.visit(
      [&](const Version<4>& selected) {
        auto serialized = selected.serialize(output);
        ASSERT(serialized);
        serialized.visit(
            [&](View::Bytes text) { EXPECT_TEXT(text, "1.2.0.0"_view); },
            []() {});
      },

      []() {});
}

VALIDATION_TEST(SerializationVersion, serialize_component_limit) {
  constexpr Version<4> version = {
    {U16(-1), U16(-1), U16(-1), U16(-1)},
  };

  Storage::Bytes<Version<4>::get_max_serialized_size()> output;
  auto serialized = version.serialize(output);

  ASSERT(serialized);
  serialized.visit(
      [&](View::Bytes text) {
        EXPECT_TEXT(text, "65535.65535.65535.65535"_view);
      },

      []() {});
}

VALIDATION_TEST(SerializationVersion, serialize_preserves_small_output) {
  constexpr Version<2> version = {{12, 34}};
  Storage::Bytes<4> output = {{'k', 'e', 'e', 'p'}};

  EXPECT_NOT(version.serialize(output));
  EXPECT_TEXT(output, "keep"_view);
}
