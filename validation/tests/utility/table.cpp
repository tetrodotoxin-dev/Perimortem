// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/utility/table.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/null_terminated.hpp"
#include "perimortem/core/static/vector.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Utility;

using namespace Toolchain::Validation;

using KeywordEntry = Pair<View::Bytes, U8>;
constexpr Static::Vector<KeywordEntry, 23> keyword_source = {{
  KeywordEntry{"as"_view, 1},
  {"if"_view, 2},
  {"for"_view, 3},
  {"new"_view, 4},
  {"else"_view, 5},
  {"func"_view, 6},
  {"init"_view, 7},
  {"self"_view, 8},
  {"true"_view, 9},
  {"alias"_view, 10},
  {"debug"_view, 11},
  {"error"_view, 12},
  {"false"_view, 13},
  {"using"_view, 14},
  {"while"_view, 15},
  {"entity"_view, 16},
  {"object"_view, 17},
  {"return"_view, 18},
  {"struct"_view, 19},
  {"library"_view, 20},
  {"on_load"_view, 21},
  {"package"_view, 22},
  {"warning"_view, 23},
}};

using keyword_table = Table<U32, keyword_source>;
static_assert(
    __is_same(decltype(keyword_table::find("as"_view)), Option<const U32&>));

using WordEntry = Pair<View::Bytes, View::Bytes>;
constexpr Static::Vector<WordEntry, 8> word_source = {{
  WordEntry{"a"_view, "b"_view},
  {"b"_view, "test"_view},
  {"longer?"_view, "shorter"_view},
  {"possible?"_view, "maybe!"_view},
  {"happy"_view, "feeling glad"_view},
  {"sunshine"_view, "in a bag"_view},
  {"useless"_view, "not for long"_view},
  {"future"_view, "comming on"_view},
}};

using word_table = Table<View::Bytes, word_source>;

struct TypeMetadata {
  View::Bytes name{};
  Count byte_size = 0;
  Bool exposed = False;
};

using MetadataEntry = Pair<View::Bytes, TypeMetadata>;
constexpr Static::Vector<MetadataEntry, 3> metadata_source = {{
  MetadataEntry{"Vec2D"_view, {"Vec2D"_view, 8, True}},
  {"Vec3D"_view, {"Vec3D"_view, 12, True}},
  {"Sampler2D"_view, {"Sampler2D"_view, 0, False}},
}};

using metadata_table = Table<TypeMetadata, metadata_source>;

constexpr auto make_limit_key(Count index) -> U8 {
  return U8(index);
}

constexpr Static::Vector<U8, U8(-1)> limit_keys(make_limit_key);

using LimitEntry = Pair<View::Bytes, U8>;
constexpr auto make_limit_entry(Count index) -> LimitEntry {
  return {View::Bytes(limit_keys.get_data() + index, 1), U8(index)};
}

constexpr Static::Vector<LimitEntry, U8(-1)> limit_source(make_limit_entry);
using limit_table = Table<U8, limit_source>;

static Harness StaticTable = {
  .name = "Utility::Table",
};

VALIDATION_TEST(StaticTable, keyword_table) {
  for (Count i = 0; i < keyword_source.get_size(); i++) {
    auto value = keyword_table::find(keyword_source[i].key);
    ASSERT(value);
    value.visit(
        [&](U32 selected) { EXPECT_EQ(selected, keyword_source[i].value); },
        []() {});
  }

  EXPECT_NOT(keyword_table::find("unknown"_view));
  EXPECT_NOT(keyword_table::find("a"_view));
  EXPECT_NOT(keyword_table::find("As"_view));
  EXPECT_NOT(keyword_table::find(""_view));
  EXPECT_NOT(keyword_table::find("rutern"_view));
  EXPECT_NOT(keyword_table::find("errrr"_view));
}

VALIDATION_TEST(StaticTable, word_table) {
  for (Count i = 0; i < word_source.get_size(); i++) {
    auto value = word_table::find(word_source[i].key);
    ASSERT(value);
    value.visit(
        [&](View::Bytes selected) {
          EXPECT_TEXT(selected, word_source[i].value);
        },

        []() {});
  }

  EXPECT_NOT(word_table::find("unknown"_view));
  EXPECT_NOT(word_table::find("c"_view));
  EXPECT_NOT(word_table::find("As"_view));
  EXPECT_NOT(word_table::find(""_view));
  EXPECT_NOT(word_table::find("rutern"_view));
  EXPECT_NOT(word_table::find("errrr"_view));
}

VALIDATION_TEST(StaticTable, find) {
  const auto vec = metadata_table::find("Vec3D"_view);
  ASSERT(vec);
  vec.visit(
      [&](const TypeMetadata& metadata) {
        EXPECT_TEXT(metadata.name, "Vec3D"_view);
        EXPECT_EQ(metadata.byte_size, 12);
        EXPECT(metadata.exposed);
      },

      [] {});

  EXPECT_NOT(metadata_table::find("Missing"_view));
  EXPECT_NOT(metadata_table::find(""_view));
}

VALIDATION_TEST(StaticTable, entry_limit) {
  for (Count i = 0; i < limit_source.get_size(); i++) {
    auto value = limit_table::find(limit_source[i].key);
    ASSERT(value);
    value.visit([&](U8 selected) { EXPECT_EQ(selected, U8(i)); }, []() {});
  }
}
