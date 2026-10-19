// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/memory/managed/bytes.hpp"
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

static_assert(__is_constructible(Formats::Json::Blueprint, S64));
static_assert(__is_constructible(Formats::Json::Blueprint, U32));
static_assert(!__is_constructible(Formats::Json::Blueprint, U64));
static_assert(!__is_constructible(Formats::Json::Blueprint, View::Bytes, U64));

static auto parse_into(
    Allocator::Arena& arena,
    View::Bytes source,
    Formats::Json::Node& output) -> Bool {
  Bool parsed = False;
  const auto accept = [&](const Formats::Json::Node& value) {
    output = value;
    parsed = True;
  };

  const auto reject = [] {};
  Formats::Json::Node::parse(arena, source).visit(accept, reject);
  return parsed;
}

static auto parse_rejects(Allocator::Arena& arena, View::Bytes source) -> Bool {
  Bool rejected = False;
  const auto accept = [](const Formats::Json::Node&) {};
  const auto reject = [&] { rejected = True; };

  Formats::Json::Node::parse(arena, source).visit(accept, reject);
  return rejected;
}

static auto format_value(
    Allocator::Arena& arena,
    const Formats::Json::Node& value) -> View::Bytes {
  return value.format(arena);
}

static auto format_rejects(
    Allocator::Arena& arena,
    const Formats::Json::Node& value) -> Bool {
  return value.format(arena).is_empty();
}

VALIDATION_TEST(SerializationJson, empty_node) {
  Formats::Json::Node empty;

  EXPECT(empty.is_null());

  EXPECT_NOT(empty.get_boolean());
  EXPECT_EQ(empty.get_integer(), 0);
  EXPECT_EQ(empty.get_real(), 0.0);
  EXPECT_EQ(empty.get_size(), 0);
  EXPECT_TEXT(empty.get_string(), View::Bytes());
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
  EXPECT(parse_into(arena, "true"_view, value));
  EXPECT(value.get_boolean());

  EXPECT(parse_into(arena, "false"_view, value));
  EXPECT_NOT(value.get_boolean());

  EXPECT(parse_into(arena, "12"_view, value));
  EXPECT_EQ(value.get_integer(), 12);

  EXPECT(parse_into(arena, "-9000"_view, value));
  EXPECT_EQ(value.get_integer(), -9000);

  EXPECT(parse_into(arena, "12.06"_view, value));
  EXPECT_EQ(value.get_real(), 12.06);

  EXPECT(parse_into(arena, "-130.4"_view, value));
  EXPECT_EQ(value.get_real(), -130.4);

  EXPECT(parse_into(arena, "\"Sub View\""_view, value));
  EXPECT_TEXT(value.get_string(), "Sub View"_view);

  EXPECT(parse_into(arena, "\"\""_view, value));
  EXPECT_TEXT(value.get_string(), ""_view);
}

VALIDATION_TEST(SerializationJson, reject_incomplete_strings) {
  Allocator::Arena arena;
  EXPECT(parse_rejects(arena, "\"unterminated"_view));
  EXPECT(parse_rejects(arena, "\"trailing escape\\"_view));
  EXPECT(parse_rejects(arena, "\"invalid \\q escape\""_view));
}

VALIDATION_TEST(SerializationJson, relaxed_container_separators) {
  Allocator::Arena arena;
  Formats::Json::Node value;
  EXPECT(parse_into(arena, "[,,1 2,,3,]"_view, value));
  EXPECT_EQ(value.get_size(), 3);
  EXPECT_TEXT(format_value(arena, value), "[1,2,3]"_view);

  EXPECT(parse_into(arena, "{,,\"a\":1 \"b\":2,}"_view, value));
  EXPECT_EQ(value.get_size(), 2);
  EXPECT_TEXT(format_value(arena, value), "{\"a\":1,\"b\":2}"_view);

  EXPECT(parse_into(arena, "[1true]"_view, value));
  EXPECT_EQ(value.get_size(), 2);
  EXPECT(parse_rejects(arena, "{\"value\" 1}"_view));

  const U8 control_separated[] = {'[', '1', 0x00, 0x01, '2', ']'};
  EXPECT(parse_into(arena, View::Bytes(control_separated), value));
  ASSERT_EQ(value.get_size(), 2);
  EXPECT_EQ(value[0].get_integer(), 1);
  EXPECT_EQ(value[1].get_integer(), 2);
}

VALIDATION_TEST(SerializationJson, require_complete_root_value) {
  Allocator::Arena arena;
  EXPECT(parse_rejects(arena, View::Bytes()));
  EXPECT(parse_rejects(arena, "garbage true"_view));
  EXPECT(parse_rejects(arena, "true trailing"_view));

  constexpr View::Bytes incomplete_literals[] = {
    "t"_view, "tru"_view, "f"_view, "fals"_view, "n"_view, "nul"_view,
  };

  for (View::Bytes source : incomplete_literals) {
    EXPECT(parse_rejects(arena, source));
  }

  Formats::Json::Node value;
  constexpr auto padded = "true \n\t"_view;
  EXPECT(parse_into(arena, padded, value));
  EXPECT(value.get_boolean());
}

VALIDATION_TEST(SerializationJson, number_boundaries) {
  Allocator::Arena arena;
  Formats::Json::Node maximum;
  EXPECT(parse_into(arena, "9223372036854775807"_view, maximum));
  EXPECT_EQ(maximum.get_integer(), S64(9223372036854775807));

  Formats::Json::Node minimum;
  EXPECT(parse_into(arena, "-9223372036854775808"_view, minimum));
  EXPECT_EQ(minimum.get_integer(), S64(-9223372036854775807LL - 1));

  EXPECT(parse_rejects(arena, "9223372036854775808"_view));
  EXPECT(parse_rejects(arena, "-9223372036854775809"_view));
  EXPECT(parse_rejects(arena, "1e"_view));
  EXPECT(parse_rejects(arena, "1e3"_view));
  EXPECT(parse_rejects(arena, "+1"_view));
  EXPECT(parse_rejects(arena, "00000000000000000000"_view));

  Formats::Json::Node rounded_real;
  EXPECT(parse_into(arena, "9223372036854775807.0"_view, rounded_real));
  EXPECT(format_rejects(arena, rounded_real));

  Formats::Json::Node leading_zero;
  EXPECT(parse_into(arena, "01"_view, leading_zero));
  EXPECT_EQ(leading_zero.get_integer(), 1);

  Formats::Json::Node trailing_point;
  EXPECT(parse_into(arena, "1."_view, trailing_point));
  EXPECT_EQ(trailing_point.get_real(), R64(1));

  Formats::Json::Node fraction;
  constexpr auto fraction_source = "0.123456789012345678901234567890"_view;
  EXPECT(parse_into(arena, fraction_source, fraction));
  EXPECT_EQ(fraction.get_real(), R64(0.123456789012345678901234567890));
}

VALIDATION_TEST(SerializationJson, format_member_name_escapes) {
  Allocator::Arena arena;
  Formats::Json::Node value(S64(1));
  Formats::Json::Node::Member member("quote\"slash\\line\n"_view, value);
  Formats::Json::Node object(
      View::Vector<Formats::Json::Node::Member>(member, 1));

  EXPECT_TEXT(
      format_value(arena, object), "{\"quote\\\"slash\\\\line\\n\":1}"_view);
}

VALIDATION_TEST(SerializationJson, format_rejects_invalid_values) {
  Allocator::Arena arena;
  Formats::Json::Node infinite(__builtin_inf());
  EXPECT(format_rejects(arena, infinite));

  Formats::Json::Node too_large(R64(9'223'372'036'854'775'808.0));
  EXPECT(format_rejects(arena, too_large));

  const Formats::Json::Blueprint values[] = {R64(__builtin_nan(""))};
  const auto array = Formats::Json::Blueprint::array(values).construct(arena);
  EXPECT(format_rejects(arena, array));

  Formats::Json::Node cyclic;
  const Formats::Json::Node::Member member("self"_view, cyclic);
  cyclic.set(View::Vector<Formats::Json::Node::Member>(member, 1));
  EXPECT(format_rejects(arena, cyclic));
}

VALIDATION_TEST(SerializationJson, string_boundaries) {
  Allocator::Arena arena;
  Formats::Json::Node parsed;
  EXPECT(parse_into(
      arena,
      "{\"text\":\"state label : Text = \\\"Icon\\\";\",\"next\":1}"_view,
      parsed));

  ASSERT(parsed.is_object());
  EXPECT_NOT(parsed["text"_view].get_string().is_empty());
  EXPECT_EQ(parsed["next"_view].get_integer(), 1);

  Formats::Json::Node source("line\n\"title\"\\end"_view);
  EXPECT_TEXT(
      format_value(arena, source), "\"line\\n\\\"title\\\"\\\\end\""_view);

  const U8 controls[] = {
    0x00, 0x01, 0x1F, '\b', '\f', '\n', '\r', '\t', '"', '\\', '/',
  };

  const View::Bytes control_bytes(controls);
  Formats::Json::Node control_string(control_bytes);
  const View::Bytes formatted_controls = format_value(arena, control_string);
  EXPECT_TEXT(
      formatted_controls,
      "\"\\u0000\\u0001\\u001F\\b\\f\\n\\r\\t\\\"\\\\/\""_view);

  Formats::Json::Node roundtrip;
  ASSERT(parse_into(arena, formatted_controls, roundtrip));
  ASSERT_EQ(roundtrip.get_string().get_size(), sizeof(controls));
  for (Count i = 0; i < sizeof(controls); i++) {
    EXPECT_EQ(roundtrip.get_string()[i], controls[i]);
  }
}

VALIDATION_TEST(SerializationJson, string_escapes) {
  Allocator::Arena arena;
  Formats::Json::Node value;
  EXPECT(parse_into(
      arena,
      "\"quote: \\\" slash: \\\\ solidus: \\/ controls: "
      "\\b\\f\\n\\r\\t\""_view,
      value));

  EXPECT_TEXT(
      value.get_string(),
      "quote: \" slash: \\ solidus: / controls: \b\f\n\r\t"_view);

  Formats::Json::Node escaped_byte;
  ASSERT(parse_into(arena, "\"\\u0041\\u00aF\""_view, escaped_byte));
  ASSERT_EQ(escaped_byte.get_string().get_size(), 2);
  EXPECT_EQ(escaped_byte.get_string()[0], U8('A'));
  EXPECT_EQ(escaped_byte.get_string()[1], U8(0xAF));

  constexpr View::Bytes unsupported_escapes[] = {
    "\"\\u0100\""_view,
    "\"\\uD800\""_view,
    "\"\\u00G0\""_view,
    "\"\\u001\""_view,
  };

  for (View::Bytes source : unsupported_escapes) {
    EXPECT(parse_rejects(arena, source));
  }

  Formats::Json::Node malformed("kept \\uD800 and \\q"_view);
  EXPECT_TEXT(malformed.get_string(), "kept \\uD800 and \\q"_view);

  Formats::Json::Node not_a_string(S64(42));
  EXPECT_TEXT(not_a_string.get_string(), View::Bytes());
}

VALIDATION_TEST(SerializationJson, string_storage_lifetimes) {
  constexpr auto plain_source = "\"stable\""_view;
  constexpr auto escaped_source = "\"line\\nend\""_view;
  constexpr auto object_view = "{\"name\":\"value\"}"_view;

  Allocator::Arena arena;
  Formats::Json::Node plain;
  ASSERT(parse_into(arena, plain_source, plain));
  EXPECT(plain.get_string().get_data() == plain_source.get_data() + 1);

  Formats::Json::Node escaped;
  ASSERT(parse_into(arena, escaped_source, escaped));
  EXPECT(escaped.get_string().get_data() != escaped_source.get_data() + 1);
  EXPECT_TEXT(escaped.get_string(), "line\nend"_view);
  EXPECT_TEXT(format_value(arena, escaped), escaped_source);

  Formats::Json::Node referenced_object;
  ASSERT(parse_into(arena, object_view, referenced_object));

  const View::Vector<Formats::Json::Node::Member> members =
      referenced_object.get_object();
  ASSERT_EQ(members.get_size(), 1);

  const Formats::Json::Node::Member& member = members.get_data()[0];
  EXPECT(member.name.get_data() == object_view.get_data() + 2);
  EXPECT(member.node.get_string().get_data() == object_view.get_data() + 9);

  Managed::Bytes managed(arena, escaped_source);
  U8* managed_data = managed.get_data();
  Formats::Json::Node consumed;
  Bool parsed = False;
  const auto accept = [&](const Formats::Json::Node& value) {
    consumed = value;
    parsed = True;
  };

  const auto reject = [] {};
  Formats::Json::Node::parse(Data::take(managed)).visit(accept, reject);

  ASSERT(parsed);
  EXPECT(managed.is_empty());
  EXPECT(consumed.get_string().get_data() == managed_data + 1);
  EXPECT_TEXT(consumed.get_string(), "line\nend"_view);

  Managed::Bytes object_source(
      arena, "{\"na\\nme\":\"left\\u0001right\",\"next\":7}"_view);
  Formats::Json::Node object;
  parsed = False;

  const auto accept_object = [&](const Formats::Json::Node& value) {
    object = value;
    parsed = True;
  };

  Formats::Json::Node::parse(Data::take(object_source))
      .visit(accept_object, reject);
  ASSERT(parsed);
  EXPECT(object_source.is_empty());
  EXPECT(object.contains("na\nme"_view));

  const View::Bytes text = object["na\nme"_view].get_string();
  ASSERT_EQ(text.get_size(), 10);
  EXPECT_TEXT(text.slice(0, 4), "left"_view);
  EXPECT_EQ(text[4], U8(0x01));
  EXPECT_TEXT(text.slice(5), "right"_view);
  EXPECT_EQ(object["next"_view].get_integer(), 7);

  Managed::Bytes invalid(arena, "{"_view);
  Bool rejected = False;
  const auto unexpected = [](const Formats::Json::Node&) {};
  const auto expected = [&] { rejected = True; };

  Formats::Json::Node::parse(Data::take(invalid)).visit(unexpected, expected);
  EXPECT(rejected);
  EXPECT(invalid.is_empty());
}

VALIDATION_TEST(SerializationJson, arbitrary_string_bytes) {
  const U8 source[] = {'"', 0xC0, '\n', '"'};
  Allocator::Arena arena;
  Formats::Json::Node value;
  ASSERT(parse_into(arena, View::Bytes(source), value));
  EXPECT_EQ(value.get_string()[0], U8(0xC0));
  EXPECT_EQ(format_value(arena, value)[1], U8(0xC0));
  EXPECT_TEXT(format_value(arena, value).slice(2), "\\n\""_view);
}

VALIDATION_TEST(SerializationJson, long_string_scan) {
  Allocator::Arena arena;
  Managed::Bytes source(arena, 96);
  Formats::Json::Node value;
  for (Count prefix_size = 64; prefix_size < 80; prefix_size++) {
    source.clear();
    source.append('"');
    source.append('a', prefix_size);
    source.concat("\\nb\""_view);

    ASSERT(parse_into(arena, source.get_view(), value));

    const View::Bytes text = value.get_string();
    ASSERT_EQ(text.get_size(), prefix_size + 2);
    EXPECT_EQ(text[prefix_size], U8('\n'));
    EXPECT_EQ(text[prefix_size + 1], U8('b'));
  }

  source.clear();
  source.append('"');
  source.append('a', 72);
  source.append(U8(0x1F));
  source.append('"');
  EXPECT(parse_into(arena, source.get_view(), value));
  EXPECT_EQ(value.get_string()[72], U8(0x1F));

  source.clear();
  source.append('"');
  source.append('a', 72);
  source.append(U8(0xC0));
  source.append('"');
  EXPECT(parse_into(arena, source.get_view(), value));
  EXPECT_EQ(value.get_string()[72], U8(0xC0));
}

VALIDATION_TEST(SerializationJson, nesting_limit) {
  Allocator::Arena arena;
  Managed::Bytes source(arena, Formats::Json::Node::nesting_limit * 2 + 3);

  for (Count depth = 0; depth < Formats::Json::Node::nesting_limit; depth++) {
    source.append('[');
  }

  source.append('0');
  for (Count depth = 0; depth < Formats::Json::Node::nesting_limit; depth++) {
    source.append(']');
  }

  Formats::Json::Node boundary;
  ASSERT(parse_into(arena, source.get_view(), boundary));
  EXPECT_NOT(format_value(arena, boundary).is_empty());

  source.clear();
  for (Count depth = 0; depth <= Formats::Json::Node::nesting_limit; depth++) {
    source.append('[');
  }

  source.append('0');
  for (Count depth = 0; depth <= Formats::Json::Node::nesting_limit; depth++) {
    source.append(']');
  }

  EXPECT(parse_rejects(arena, source.get_view()));
}

VALIDATION_TEST(SerializationJson, parse_arrays) {
  Allocator::Arena arena;
  Formats::Json::Node value;

  EXPECT(parse_into(arena, "[]"_view, value));
  ASSERT(value.is_array());
  EXPECT_EQ(value.get_size(), 0);

  EXPECT(parse_into(arena, "[1,2,3,4]"_view, value));
  ASSERT(value.is_array());
  EXPECT_EQ(value.get_size(), 4);
  EXPECT_EQ(value[0].get_integer(), 1);
  EXPECT_EQ(value[1].get_integer(), 2);
  EXPECT_EQ(value[2].get_integer(), 3);
  EXPECT_EQ(value[3].get_integer(), 4);
  EXPECT(value[4].is_null());
  EXPECT(value[4]["next"_view].is_null());

  EXPECT(parse_into(arena, "[1,true,\"test\",4]"_view, value));
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

  EXPECT(parse_into(arena, "[]"_view, value));
  ASSERT(value.is_array());
  EXPECT_EQ(value.get_size(), 0);

  EXPECT(parse_into(
      arena, "[1,[2],[[true],3,[\"value\"]],[[],[[4],[]]]]"_view, value));
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
    EXPECT(parse_rejects(arena, source));
  }
}

VALIDATION_TEST(SerializationJson, parse_object) {
  Allocator::Arena arena;
  Formats::Json::Node value;

  EXPECT(parse_into(arena, "{}"_view, value));
  ASSERT(value.is_object());
  EXPECT_EQ(value.get_size(), 0);

  EXPECT(parse_into(
      arena, "{\"value\":1,\"test\":2,\"another member\":3,\"final\":4}"_view,
      value));
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
    EXPECT(parse_rejects(arena, source));
  }
}

VALIDATION_TEST(SerializationJson, parse_null) {
  Allocator::Arena arena;
  Formats::Json::Node value;

  EXPECT(parse_into(arena, "null"_view, value));
  EXPECT(value.is_null());

  EXPECT(parse_into(arena, "{\"key\":null}"_view, value));
  ASSERT(value.is_object());
  EXPECT_EQ(value.get_size(), 1);
  EXPECT(value.contains("key"_view));
  EXPECT_NOT(value.contains("missing"_view));
  EXPECT(value["key"_view].is_null());

  EXPECT(parse_into(arena, "{\"a\":1,\"b\":null,\"c\":3}"_view, value));
  ASSERT(value.is_object());
  EXPECT_EQ(value.get_size(), 3);
  EXPECT_EQ(value["a"_view].get_integer(), 1);
  EXPECT(value["b"_view].is_null());
  EXPECT_EQ(value["c"_view].get_integer(), 3);

  // An array retains a null element as a reachable null node.
  EXPECT(parse_into(arena, "[1,null,3]"_view, value));
  ASSERT(value.is_array());
  EXPECT_EQ(value.get_size(), 3);
  EXPECT_EQ(value[0].get_integer(), 1);
  EXPECT(value[1].is_null());
  EXPECT_EQ(value[2].get_integer(), 3);
}

VALIDATION_TEST(SerializationJson, construct_string) {
  constexpr auto expected = "\"Test String\""_view;

  Allocator::Arena arena;
  Formats::Json::Node value =
      Formats::Json::Blueprint("Test String"_view).construct(arena);

  const auto formatted = format_value(arena, value);
  ASSERT_TEXT(formatted, expected);
}

VALIDATION_TEST(SerializationJson, construct_array) {
  constexpr auto expected = "[1,2,3,4]"_view;
  const Formats::Json::Blueprint elements[] = {1, 2, 3, 4};

  Allocator::Arena arena;
  Formats::Json::Node value =
      Formats::Json::Blueprint::array(elements).construct(arena);

  const auto formatted = format_value(arena, value);
  ASSERT_TEXT(formatted, expected);
}

VALIDATION_TEST(SerializationJson, construct_nested) {
  constexpr auto expected =
      "{\"root\":[1,2,true,false,\"Test\"],\"flag\":true,\"config\":{"
      "\"version\":\"1.0.2\",\"sub_flag\":true}}"_view;
  const Formats::Json::Blueprint root_values[] = {
    1, 2, True, False, "Test"_view,
  };

  const Formats::Json::Blueprint config_members[] = {
    {"version"_view, "1.0.2"_view},
    {"sub_flag"_view, True},
  };

  const Formats::Json::Blueprint members[] = {
    {"root"_view, Formats::Json::Blueprint::array(root_values)},
    {"flag"_view, True},
    {"config"_view, Formats::Json::Blueprint::object(config_members)},
  };

  Allocator::Arena arena;
  Formats::Json::Node value =
      Formats::Json::Blueprint::object(members).construct(arena);

  const auto formatted = format_value(arena, value);
  ASSERT_TEXT(formatted, expected);
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
  ASSERT(parse_into(arena, source_view, value));

  const auto formatted = format_value(arena, value);

  ASSERT_EQ(formatted.get_size(), source_view.get_size());
  ASSERT_TEXT(formatted, source_view);
}

#endif

VALIDATION_TEST(SerializationJson, format_number_zero) {
  const Formats::Json::Blueprint members[] = {
    {"a"_view, 0},
    {"b"_view, 1},
    {"c"_view, 10},
  };

  Allocator::Arena arena;
  auto value = Formats::Json::Blueprint::object(members).construct(arena);

  constexpr auto expected = "{\"a\":0,\"b\":1,\"c\":10}"_view;
  const auto formatted = format_value(arena, value);
  ASSERT_TEXT(formatted, expected);
}

VALIDATION_TEST(SerializationJson, explicit_container_shape) {
  const Formats::Json::Blueprint members[] = {
    {"name"_view, "items"_view},
    {"items"_view, Formats::Json::Blueprint::array()},
    {""_view, 1},
    {"config"_view, Formats::Json::Blueprint::object()},
  };

  Allocator::Arena arena;
  auto value = Formats::Json::Blueprint::object(members).construct(arena);

  constexpr auto expected =
      "{\"name\":\"items\",\"items\":[],\"\":1,\"config\":{}}"_view;
  const auto formatted = format_value(arena, value);
  ASSERT_TEXT(formatted, expected);
}

VALIDATION_TEST(SerializationJson, copies_descriptor_storage) {
  U8 member_name[] = {'n', 'a', 'm', 'e'};
  U8 string_value[] = {'v', 'a', 'l', 'u', 'e'};
  const Formats::Json::Blueprint members[] = {
    {View::Bytes(member_name[0], sizeof(member_name)),
     View::Bytes(string_value[0], sizeof(string_value))},
  };

  Allocator::Arena arena;
  auto value = Formats::Json::Blueprint::object(members).construct(arena);
  member_name[0] = 'x';
  string_value[0] = 'x';

  EXPECT(value.contains("name"_view));
  EXPECT_TEXT(value["name"_view].get_string(), "value"_view);
}

VALIDATION_TEST(SerializationJson, existing_node) {
  Allocator::Arena source_arena;
  const Formats::Json::Blueprint inner_members[] = {
    {"name"_view, "ttx-server"_view},
    {"version"_view, "1.0"_view},
  };

  const Formats::Json::Node inner_node =
      Formats::Json::Blueprint::object(inner_members).construct(source_arena);

  const Formats::Json::Blueprint outer_members[] = {
    {"jsonrpc"_view, "2.0"_view},
    {"id"_view, 1},
    {"result"_view, inner_node},
  };

  Allocator::Arena destination_arena;
  auto value = Formats::Json::Blueprint::object(outer_members)
                   .construct(destination_arena);
  source_arena.reset();

  auto overwritten = source_arena.allocate(1024);
  Data::set(overwritten.get_data(), 0xAA, overwritten.get_size());

  constexpr auto expected =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"name\":\"ttx-server\","
      "\"version\":\"1.0\"}}"_view;
  const auto formatted = format_value(destination_arena, value);
  ASSERT_TEXT(formatted, expected);
}

VALIDATION_TEST(SerializationJson, format_null) {
  Allocator::Arena arena;
  Formats::Json::Node value;

  EXPECT(parse_into(arena, "null"_view, value));
  ASSERT_TEXT(format_value(arena, value), "null"_view);

  EXPECT(parse_into(arena, "{\"a\":1,\"b\":null,\"c\":3}"_view, value));
  ASSERT_TEXT(format_value(arena, value), "{\"a\":1,\"b\":null,\"c\":3}"_view);
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
  ASSERT(parse_into(arena, source_view, parsed));

  ASSERT(parsed["jsonrpc"_view].is_string());
  ASSERT(parsed["id"_view].is_integer());

  const Formats::Json::Blueprint result_members[] = {
    {"serverInfo"_view, "ttx"_view},
  };

  const Formats::Json::Blueprint response_members[] = {
    {"jsonrpc"_view, parsed["jsonrpc"_view].get_string()},
    {"id"_view, parsed["id"_view].get_integer()},
    {"result"_view, Formats::Json::Blueprint::object(result_members)},
  };

  auto response =
      Formats::Json::Blueprint::object(response_members).construct(arena);

  const auto formatted = format_value(arena, response);

  ASSERT(formatted.get_size() > 0);
  ASSERT(response.is_object());
  EXPECT_TEXT(response["jsonrpc"_view].get_string(), "2.0"_view);
  EXPECT_EQ(response["id"_view].get_integer(), 10);
}

#endif
