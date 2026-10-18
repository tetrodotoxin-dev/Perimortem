// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/serialization/formats/json/blueprint.hpp"
#include "perimortem/serialization/formats/json/node.hpp"
#include "perimortem/serialization/stream/textual.hpp"
#include "perimortem/system/filesystem/file.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::Serialization;
using namespace Perimortem::System;

using namespace Toolchain::Validation;

static Harness SerializationJson = {
  .name = "Serialization::Formats::Json",
};

VALIDATION_TEST(SerializationJson, empty_node) {
  Formats::Json::Node empty;
  Allocator::Arena arena;

  EXPECT(empty.is_null());

  EXPECT_NOT(empty.get_boolean());
  EXPECT_EQ(empty.get_integer(), 0);
  EXPECT_EQ(empty.get_real(), 0.0);
  EXPECT_EQ(empty.get_size(), 0);
  EXPECT_TEXT(empty.get_string(), View::Bytes());
  EXPECT_TEXT(empty.decode_string(arena), View::Bytes());
  EXPECT(empty.get_array().is_empty());
  EXPECT(empty.get_object().is_empty());
}

VALIDATION_TEST(SerializationJson, value_node) {
  Formats::Json::Node value;
  EXPECT(value.is_null());

  value = True;
  EXPECT(value.get_boolean());
  EXPECT_EQ(value.get_integer(), 0);
  EXPECT_EQ(value.get_real(), 0.0);
  EXPECT_TEXT(value.get_string(), View::Bytes());
  EXPECT(value.get_array().is_empty());
  EXPECT(value.get_object().is_empty());
  EXPECT_EQ(value.get_size(), 0);

  value = 142ll;
  EXPECT_NOT(value.get_boolean());
  EXPECT_EQ(value.get_integer(), 142ll);
  EXPECT_EQ(value.get_real(), 0.0);
  EXPECT_TEXT(value.get_string(), View::Bytes());
  EXPECT(value.get_array().is_empty());
  EXPECT(value.get_object().is_empty());
  EXPECT_EQ(value.get_size(), 0);

  value = 12.12;
  EXPECT_NOT(value.get_boolean());
  EXPECT_EQ(value.get_integer(), 0);
  EXPECT_EQ(value.get_real(), 12.12);
  EXPECT_TEXT(value.get_string(), View::Bytes());
  EXPECT(value.get_array().is_empty());
  EXPECT(value.get_object().is_empty());
  EXPECT_EQ(value.get_size(), 0);

  value = "Test View"_view;
  EXPECT_NOT(value.get_boolean());
  EXPECT_EQ(value.get_integer(), 0);
  EXPECT_EQ(value.get_real(), 0);
  EXPECT_TEXT(value.get_string(), "Test View"_view);
  EXPECT(value.get_array().is_empty());
  EXPECT(value.get_object().is_empty());
  EXPECT_EQ(value.get_size(), 9);
}

VALIDATION_TEST(SerializationJson, access_never_faults) {
  Formats::Json::Node value;
  EXPECT(value.is_null());

  value = True;
  EXPECT(value["invalid"_view].is_null());
  EXPECT(value["invalid"_view]["access"_view].is_null());

  value = value["invalid"_view]["access"_view];

  EXPECT_NOT(value.get_boolean());
  EXPECT_EQ(value.get_integer(), 0);
  EXPECT_EQ(value.get_real(), 0.0);
  EXPECT_EQ(value.get_size(), 0);
  EXPECT_TEXT(value.get_string(), View::Bytes());
  EXPECT(value.get_array().is_empty());
  EXPECT(value.get_object().is_empty());
}

VALIDATION_TEST(SerializationJson, access_is_value_type) {
  Formats::Json::Node value;
  EXPECT(value.is_null());

  auto new_node = value["invalid"_view];
  EXPECT(new_node.is_null());

  new_node = "valid"_view;
  EXPECT_NOT(new_node.is_null());
  EXPECT_TEXT(new_node.get_string(), "valid"_view);

  EXPECT(value.is_null());
  EXPECT(value["invalid"_view].is_null());
}

VALIDATION_TEST(SerializationJson, parse_values) {
  Allocator::Arena arena;
  Formats::Json::Node value;
  value.parse(arena, ""_view);
  EXPECT(value.is_null());

  value.parse(arena, "true"_view);
  EXPECT(value.get_boolean());

  value.parse(arena, "false"_view);
  EXPECT_NOT(value.get_boolean());

  value.parse(arena, "12"_view);
  EXPECT_EQ(value.get_integer(), 12);

  value.parse(arena, "-9000"_view);
  EXPECT_EQ(value.get_integer(), -9000);

  value.parse(arena, "12.06"_view);
  EXPECT_EQ(value.get_real(), 12.06);

  value.parse(arena, "-130.4"_view);
  EXPECT_EQ(value.get_real(), -130.4);

  value.parse(arena, "\"Sub View\""_view);
  EXPECT_TEXT(value.get_string(), "Sub View"_view);

  value.parse(arena, "\"\""_view);
  EXPECT_TEXT(value.get_string(), ""_view);
}

VALIDATION_TEST(SerializationJson, string_boundaries) {
  Allocator::Arena arena;
  Formats::Json::Node parsed;
  parsed.parse(
      arena,
      "{\"text\":\"state label : Text = \\\"Icon\\\";\",\"next\":1}"_view);

  ASSERT(parsed.is_object());
  EXPECT_NOT(parsed["text"_view].get_string().is_empty());
  EXPECT_EQ(parsed["next"_view].get_integer(), 1);

  Formats::Json::Node source("line\n\"title\"\\end"_view);
  EXPECT_TEXT(source.format(arena), "\"line\\n\\\"title\\\"\\\\end\""_view);
}

VALIDATION_TEST(SerializationJson, string_escapes) {
  Allocator::Arena arena;
  Formats::Json::Node value;
  value.parse(
      arena,
      "\"quote: \\\" slash: \\\\ solidus: \\/ controls: "
      "\\b\\f\\n\\r\\t unicode: \\u0041\\u03A9\\uD83D\\uDE00\""_view);

  EXPECT_TEXT(
      value.decode_string(arena),
      "quote: \" slash: \\ solidus: / controls: \b\f\n\r\t unicode: "
      "AΩ😀"_view);

  Formats::Json::Node malformed("kept \\uD800 and \\q"_view);
  EXPECT_TEXT(malformed.decode_string(arena), "kept \\uD800 and \\q"_view);

  Formats::Json::Node not_a_string(S64(42));
  EXPECT_TEXT(not_a_string.decode_string(arena), View::Bytes());
}

VALIDATION_TEST(SerializationJson, parse_arrays) {
  Allocator::Arena arena;
  Formats::Json::Node value;

  value.parse(arena, "[]"_view);
  ASSERT(value.is_array());
  EXPECT_EQ(value.get_size(), 0);

  value.parse(arena, "[1,2,3,4]"_view);
  ASSERT(value.is_array());
  EXPECT_EQ(value.get_size(), 4);
  EXPECT_EQ(value[0].get_integer(), 1);
  EXPECT_EQ(value[1].get_integer(), 2);
  EXPECT_EQ(value[2].get_integer(), 3);
  EXPECT_EQ(value[3].get_integer(), 4);

  value.parse(arena, "[1,true,\"test\",4]"_view);
  ASSERT(value.is_array());
  EXPECT_EQ(value.get_size(), 4);
  EXPECT_EQ(value[0].get_integer(), 1);
  EXPECT(value[1].get_boolean());
  EXPECT_TEXT(value[2].get_string(), "test"_view);
  EXPECT_EQ(value[3].get_integer(), 4);
}

VALIDATION_TEST(SerializationJson, parse_nested_arrays) {
  Allocator::Arena arena;
  Formats::Json::Node value;

  value.parse(arena, "[]"_view);
  ASSERT(value.is_array());
  EXPECT_EQ(value.get_size(), 0);

  value.parse(arena, "[1,[2],[[true],3,[\"value\"]],[[],[[4],[]]]]"_view);
  ASSERT(value.is_array());
  EXPECT_EQ(value.get_size(), 4);
  EXPECT_EQ(value[0].get_integer(), 1);

  EXPECT_EQ(value[1].get_size(), 1);
  EXPECT_EQ(value[1][0].get_integer(), 2);

  EXPECT_EQ(value[2].get_size(), 3);
  EXPECT_EQ(value[2][0].get_size(), 1);
  EXPECT_EQ(value[2][1].get_size(), 0);
  EXPECT_EQ(value[2][2].get_size(), 1);
  EXPECT(value[2][0][0].get_boolean());
  EXPECT_EQ(value[2][1].get_integer(), 3);
  EXPECT_TEXT(value[2][2][0].get_string(), "value"_view);

  EXPECT_EQ(value[3][1][0][0].get_integer(), 4);
}

VALIDATION_TEST(SerializationJson, reject_truncated_arrays) {
  constexpr View::Bytes sources[] = {"["_view, "[1"_view, "[1   "_view};
  Allocator::Arena arena;
  for (View::Bytes source : sources) {
    Formats::Json::Node value;
    EXPECT_EQ(value.parse(arena, source), Count(-1));
    EXPECT(value.is_null());
  }
}

VALIDATION_TEST(SerializationJson, parse_object) {
  Allocator::Arena arena;
  Formats::Json::Node value;

  value.parse(arena, "{}"_view);
  ASSERT(value.is_object());
  EXPECT_EQ(value.get_size(), 0);

  value.parse(
      arena, "{\"value\":1,\"test\":2,\"another member\":3,\"final\":4}"_view);
  ASSERT(value.is_object());
  EXPECT_EQ(value.get_size(), 4);
  EXPECT_EQ(value["value"_view].get_integer(), 1);
  EXPECT_EQ(value["test"_view].get_integer(), 2);
  EXPECT_EQ(value["another member"_view].get_integer(), 3);
  EXPECT_EQ(value["final"_view].get_integer(), 4);
}

VALIDATION_TEST(SerializationJson, reject_truncated_objects) {
  constexpr View::Bytes sources[] = {
    "{"_view,
    "{\"key\":1"_view,
    "{\"key\":1   "_view,
  };

  Allocator::Arena arena;
  for (View::Bytes source : sources) {
    Formats::Json::Node value;
    EXPECT_EQ(value.parse(arena, source), Count(-1));
    EXPECT(value.is_null());
  }
}

VALIDATION_TEST(SerializationJson, parse_null) {
  Allocator::Arena arena;
  Formats::Json::Node value;

  value.parse(arena, "null"_view);
  EXPECT(value.is_null());

  value.parse(arena, "{\"key\":null}"_view);
  ASSERT(value.is_object());
  EXPECT_EQ(value.get_size(), 1);
  EXPECT(value["key"_view].is_null());

  value.parse(arena, "{\"a\":1,\"b\":null,\"c\":3}"_view);
  ASSERT(value.is_object());
  EXPECT_EQ(value.get_size(), 3);
  EXPECT_EQ(value["a"_view].get_integer(), 1);
  EXPECT(value["b"_view].is_null());
  EXPECT_EQ(value["c"_view].get_integer(), 3);

  // An array retains a null element as a reachable null node.
  value.parse(arena, "[1,null,3]"_view);
  ASSERT(value.is_array());
  EXPECT_EQ(value.get_size(), 3);
  EXPECT_EQ(value[0].get_integer(), 1);
  EXPECT(value[1].is_null());
  EXPECT_EQ(value[2].get_integer(), 3);
}

VALIDATION_TEST(SerializationJson, construct_string) {
  constexpr auto expected = "\"Test String\""_view;

  Allocator::Arena arena;
  Formats::Json::Node value = Formats::Json::Blueprint{
    {
      "Test String"_view,
    }}.construct(arena);

  auto formated = value.format(arena);
  ASSERT_TEXT(formated, expected);
}

VALIDATION_TEST(SerializationJson, construct_array) {
  constexpr auto expected = "[1,2,3,4]"_view;
  const Formats::Json::Blueprint elements[] = {1, 2, 3, 4};

  Allocator::Arena arena;
  Formats::Json::Node value =
      Formats::Json::Blueprint(elements).construct(arena);

  auto formated = value.format(arena);
  ASSERT_TEXT(formated, expected);
}

VALIDATION_TEST(SerializationJson, construct_nested) {
  constexpr auto expected =
      "{\"root\":[1,2,true,false,\"Test\"],\"flag\":true,\"config\":{"
      "\"version\":\"1.0.2\",\"sub_flag\":true}}"_view;

  Allocator::Arena arena;
  Formats::Json::Node value = Formats::Json::Blueprint{
    {
      {"root"_view,
       {
         1,
         2,
         True,
         False,
         "Test"_view,
       }},

      {"flag"_view, True},
      {"config"_view,
       {
         {"version"_view, "1.0.2"_view},
         {"sub_flag"_view, True},
       }},
    }}.construct(arena);

  auto formated = value.format(arena);
  ASSERT_TEXT(formated, expected);
}

#if defined(TOOLCHAIN_LINUX) || defined(TOOLCHAIN_WINDOWS)

VALIDATION_TEST(SerializationJson, round_trip_init_rpc) {
  auto source =
      Filesystem::File::read("[work]:/validation/data/json/init_rpc.json"_path);
  ASSERT(source);

  View::Bytes source_view;
  source.visit(
      [&](const Global::Bytes& bytes) { source_view = bytes.get_view(); },
      []() {});
  ASSERT_NOT(source_view.is_empty());

  Allocator::Arena arena;
  Formats::Json::Node value;
  value.parse(arena, source_view);

  auto formated = value.format(arena);

  ASSERT_EQ(formated.get_size(), source_view.get_size());
  ASSERT_TEXT(formated, source_view);
}

#endif

VALIDATION_TEST(SerializationJson, format_number_zero) {
  Allocator::Arena arena;
  auto value = Formats::Json::Blueprint{
    {
      {"a"_view, 0},
      {"b"_view, 1},
      {"c"_view, 10},
    }}.construct(arena);

  constexpr auto expected = "{\"a\":0,\"b\":1,\"c\":10}"_view;
  auto formated = value.format(arena);
  ASSERT_TEXT(formated, expected);
}

VALIDATION_TEST(SerializationJson, empty_array) {
  Allocator::Arena arena;
  auto value = Formats::Json::Blueprint{
    {
      {"name"_view, "items"_view},
      Formats::Json::Blueprint::empty_array("items"_view),
    }}.construct(arena);

  constexpr auto expected = "{\"name\":\"items\",\"items\":[]}"_view;
  auto formated = value.format(arena);
  ASSERT_TEXT(formated, expected);
}

VALIDATION_TEST(SerializationJson, existing_node) {
  Allocator::Arena arena;

  const Formats::Json::Node inner_node = Formats::Json::Blueprint{
    {
      {"name"_view, "ttx-server"_view},
      {"version"_view, "1.0"_view},
    }}.construct(arena);

  auto value = Formats::Json::Blueprint{
    {
      {"jsonrpc"_view, "2.0"_view},
      {"id"_view, 1},
      {"result"_view, inner_node},
    }}.construct(arena);

  constexpr auto expected =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"name\":\"ttx-server\","
      "\"version\":\"1.0\"}}"_view;
  auto formated = value.format(arena);
  ASSERT_TEXT(formated, expected);
}

VALIDATION_TEST(SerializationJson, format_null) {
  Allocator::Arena arena;
  Formats::Json::Node value;

  value.parse(arena, "null"_view);
  ASSERT_TEXT(value.format(arena), "null"_view);

  value.parse(arena, "{\"a\":1,\"b\":null,\"c\":3}"_view);
  ASSERT_TEXT(value.format(arena), "{\"a\":1,\"b\":null,\"c\":3}"_view);
}

#if defined(TOOLCHAIN_LINUX) || defined(TOOLCHAIN_WINDOWS)

VALIDATION_TEST(SerializationJson, rpc_from_parsed) {
  auto source =
      Filesystem::File::read("[work]:/validation/data/json/init_rpc.json"_path);
  ASSERT(source);

  View::Bytes source_view;
  source.visit(
      [&](const Global::Bytes& bytes) { source_view = bytes.get_view(); },
      []() {});
  ASSERT_NOT(source_view.is_empty());

  Allocator::Arena arena;
  Formats::Json::Node parsed;
  parsed.parse(arena, source_view);

  ASSERT(parsed["jsonrpc"_view].is_string());
  ASSERT(parsed["id"_view].is_integer());

  auto response = Formats::Json::Blueprint{
    {
      {"jsonrpc"_view, parsed["jsonrpc"_view].get_string()},
      {"id"_view, parsed["id"_view].get_integer()},
      {"result"_view,
       {
         {"serverInfo"_view, "ttx"_view},
       }},
    }}.construct(arena);

  auto formated = response.format(arena);

  ASSERT(formated.get_size() > 0);
  ASSERT(response.is_object());
  EXPECT_TEXT(response["jsonrpc"_view].get_string(), "2.0"_view);
  EXPECT_EQ(response["id"_view].get_integer(), 10);
}

#endif
