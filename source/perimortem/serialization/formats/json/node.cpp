// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/serialization/formats/json/node.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/memory/managed/bytes.hpp"
#include "perimortem/memory/managed/vector.hpp"
#include "perimortem/serialization/stream/textual.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::Serialization;

static auto decode_hexadecimal(U8 digit, U8& value) -> Bool {
  if (digit >= '0' && digit <= '9') {
    value = digit - '0';
    return True;
  }

  if (digit >= 'A' && digit <= 'F') {
    value = digit - 'A' + 10;
    return True;
  }

  if (digit >= 'a' && digit <= 'f') {
    value = digit - 'a' + 10;
    return True;
  }

  return False;
}

// Strings preserve bytes rather than assigning Unicode semantics. The short
// JSON escapes map directly to one byte. Only `\u00XX` is accepted because
// formatting uses it to preserve arbitrary control bytes.
static auto
    decode_escape(const U8* source, Count size, U8& value, Count& consumed)
        -> Bool {
  if (size == 0) {
    return False;
  }

  consumed = 1;
  switch (source[0]) {
  case '"':
  case '\\':
  case '/':
    value = source[0];
    return True;
  case 'b':
    value = '\b';
    return True;
  case 'f':
    value = '\f';
    return True;
  case 'n':
    value = '\n';
    return True;
  case 'r':
    value = '\r';
    return True;
  case 't':
    value = '\t';
    return True;
  case 'u': {
    if (size < 5 || source[1] != '0' || source[2] != '0') {
      return False;
    }

    U8 upper = 0;
    U8 lower = 0;
    if (!decode_hexadecimal(source[3], upper) ||
        !decode_hexadecimal(source[4], lower)) {
      return False;
    }

    value = U8((upper << 4) | lower);
    consumed = 5;
    return True;
  }

  default:
    return False;
  }
}

// Between values the profile groups every byte through ASCII space into one
// separator class. Strings are scanned separately, so those same C0 bytes
// remain content inside quotes.
static auto is_json_whitespace(U8 value) -> Bool {
  return value <= ' ';
}

static constexpr auto zero_byte_mask(U64 value) -> U64 {
  constexpr U64 low_bits = 0x0101010101010101;
  constexpr U64 high_bits = 0x8080808080808080;
  return (value - low_bits) & ~value & high_bits;
}

auto Formats::Json::Node::set(const View::Bytes value) -> void {
  data.ptr = value.get_data();
  data.size = value.get_size();
  data.state = U32(State::String);
}

auto Formats::Json::Node::set(const View::Vector<Node> value) -> void {
  data.ptr = value.get_data();
  data.size = value.get_size();
  data.state = U32(State::Array);
}

auto Formats::Json::Node::set(const View::Vector<Member> value) -> void {
  data.ptr = value.get_data();
  data.size = value.get_size();
  data.state = U32(State::Object);
}

auto Formats::Json::Node::set(S64 value) -> void {
  data.integer = value;
  data.state = U32(State::Integer);
}

auto Formats::Json::Node::set(R64 value) -> void {
  data.real = value;
  data.state = U32(State::Real);
}

auto Formats::Json::Node::set(Bool value) -> void {
  data.boolean = value;
  data.state = U32(State::Boolean);
}

auto Formats::Json::Node::set() -> void {
  data.state = U32(State::Null);
}

auto Formats::Json::Node::operator[](U32 index) const -> Formats::Json::Node {
  if (data.state == U32(State::Array) && index < data.size) {
    const auto values = Data::cast<const Formats::Json::Node>(data.ptr);
    return values[index];
  }

  return Formats::Json::Node();
}

auto Formats::Json::Node::operator[](const View::Bytes name) const
    -> Formats::Json::Node {
  if (data.state == U32(State::Object) && data.size != 0) {
    const auto members = Data::cast<const Member>(data.ptr);
    for (Count i = 0; i < data.size; i++) {
      if (members[i].name == name) {
        return members[i].node;
      }
    }
  }

  return Formats::Json::Node();
}

auto Formats::Json::Node::contains(const View::Bytes name) const -> Bool {
  if (data.state == U32(State::Object) && data.size != 0) {
    const auto members = Data::cast<const Member>(data.ptr);
    for (Count i = 0; i < data.size; i++) {
      if (members[i].name == name) {
        return True;
      }
    }
  }

  return False;
}

auto Formats::Json::Node::get_boolean() const -> Bool {
  if (data.state == U32(State::Boolean)) {
    return data.boolean;
  }

  return false;
}

auto Formats::Json::Node::get_integer() const -> S64 {
  if (data.state == U32(State::Integer)) {
    return data.integer;
  }

  return 0;
}

auto Formats::Json::Node::get_real() const -> R64 {
  if (data.state == U32(State::Real)) {
    return data.real;
  }

  return 0.0;
}

auto Formats::Json::Node::get_string() const -> View::Bytes {
  if (data.state == U32(State::String) && data.size != 0) {
    const auto values = Data::cast<const U8>(data.ptr);
    return View::Bytes(values[0], data.size);
  }

  return View::Bytes();
}

auto Formats::Json::Node::get_array() const -> View::Vector<Node> {
  if (data.state == U32(State::Array) && data.size != 0) {
    const auto values = Data::cast<const Node>(data.ptr);
    return View::Vector<Node>(values[0], data.size);
  }

  return View::Vector<Node>();
}

auto Formats::Json::Node::get_object() const -> View::Vector<Member> {
  if (data.state == U32(State::Object) && data.size != 0) {
    const auto members = Data::cast<const Member>(data.ptr);
    return View::Vector<Member>(members[0], data.size);
  }

  return View::Vector<Member>();
}

auto Formats::Json::Node::get_size() const -> Count {
  switch (State(data.state)) {
  case State::String:
  case State::Array:
  case State::Object:
    return data.size;

  default:
    return 0;
  }
}

auto Formats::Json::Node::is_null() const -> Bool {
  return State(data.state) == State::Null;
}

auto Formats::Json::Node::is_boolean() const -> Bool {
  return State(data.state) == State::Boolean;
}

auto Formats::Json::Node::is_integer() const -> Bool {
  return State(data.state) == State::Integer;
}

auto Formats::Json::Node::is_real() const -> Bool {
  return State(data.state) == State::Real;
}

auto Formats::Json::Node::is_string() const -> Bool {
  return State(data.state) == State::String;
}

auto Formats::Json::Node::is_array() const -> Bool {
  return State(data.state) == State::Array;
}

auto Formats::Json::Node::is_object() const -> Bool {
  return State(data.state) == State::Object;
}

static constexpr auto is_json_digit(U8 value) -> Bool {
  return value >= '0' && value <= '9';
}

static auto skip_json_whitespace(
    View::Bytes source,
    const U8* data,
    Count& position,
    Bool commas = False) -> void {
  // Containers enable comma skipping before each value. Consuming zero or more
  // commas makes them optional, repeatable and valid before a closing token.
  while (position < source.get_size() && (is_json_whitespace(data[position]) ||
                                          (commas && data[position] == ','))) {
    position++;
  }
}

static auto parse_json_string(
    Allocator::Arena& arena,
    View::Bytes source,
    U8* mutable_source,
    Count& position,
    View::Bytes& value) -> Bool {
  const U8* data = source.get_data();
  const Count start = ++position;
  Bool escaped = False;
  while (position < source.get_size()) {
    constexpr U64 quotes = 0x2222222222222222;
    constexpr U64 slashes = 0x5C5C5C5C5C5C5C5C;
    while (source.get_size() - position >= sizeof(U64)) {
      const U64 word =
          Data::read<Data::ByteOrder::Little, U64>(data + position);
      const U64 special =
          zero_byte_mask(word ^ quotes) | zero_byte_mask(word ^ slashes);
      if (special != 0) {
        // Each matching byte contributes its high bit to the mask. Its
        // trailing zero count therefore locates the first quote or escape
        // without rescanning the preceding bytes.
        position += Count(__builtin_ctzll(special)) / 8;
        break;
      }

      position += sizeof(U64);
    }

    if (position == source.get_size()) {
      break;
    }

    const U8 byte = data[position];
    if (byte == '"') {
      const Count end = position++;
      if (!escaped) {
        value = source.slice(start, end - start);
        return True;
      }

      const Count encoded_size = end - start;
      // View parsing allocates decoded bytes. Managed parsing supplies writable
      // source storage, so decoding compacts the string inside its original
      // window without moving any later JSON bytes.
      U8* decoded = mutable_source == nullptr
                        ? arena.allocate(encoded_size).get_data()
                        : mutable_source + start;
      Count read_position = start;
      Count write_position = 0;
      while (read_position < end) {
        if (data[read_position] != '\\') {
          decoded[write_position++] = data[read_position++];
          continue;
        }

        U8 decoded_byte = 0;
        Count consumed = 0;
        if (!decode_escape(
                data + read_position + 1, end - read_position - 1, decoded_byte,
                consumed)) {
          __builtin_unreachable();
        }

        decoded[write_position++] = decoded_byte;
        read_position += consumed + 1;
      }

      value = View::Bytes(decoded[0], write_position);
      return True;
    }

    if (byte != '\\') {
      position++;
      continue;
    }

    U8 decoded_byte = 0;
    Count consumed = 0;
    if (!decode_escape(
            data + position + 1, source.get_size() - position - 1, decoded_byte,
            consumed)) {
      return False;
    }

    escaped = True;
    position += consumed + 1;
  }

  return False;
}

static auto parse_json_number(
    View::Bytes source,
    Count& position,
    Formats::Json::Node& value) -> Bool {
  const U8* data = source.get_data();
  constexpr U64 signed_maximum = U64(-1) >> 1;
  constexpr U64 signed_minimum_magnitude = signed_maximum + 1;
  constexpr S64 signed_minimum = -9223372036854775807LL - 1;

  Bool negative = False;
  if (data[position] == '-') {
    negative = True;
    position++;
    if (position == source.get_size() || !is_json_digit(data[position])) {
      return False;
    }
  }

  const U64 integer_limit =
      negative ? signed_minimum_magnitude : signed_maximum;
  U64 integer = 0;
  Count digits = 0;
  // The profile bounds numeric tokens to 19 magnitude digits even when leading
  // zeroes would make a longer spelling representable. Rejecting the twentieth
  // digit before multiplication keeps accumulation defined, then one final
  // comparison applies the signed range selected above.
  while (position < source.get_size() && is_json_digit(data[position])) {
    if (digits == 19) {
      return False;
    }

    integer = integer * 10 + data[position] - '0';
    digits++;
    position++;
  }

  if (integer > integer_limit) {
    return False;
  }

  if (position < source.get_size() && data[position] == '.') {
    position++;

    // Converting the largest positive integer part can round to 2^63. Parsing
    // remains safe, while formatting returns an empty View because that rounded
    // value is outside the profile's publishable range.
    R64 selected = R64(integer);
    R64 place = 0.1;
    while (position < source.get_size() && is_json_digit(data[position])) {
      selected += R64(data[position] - '0') * place;
      place *= 0.1;
      position++;
    }

    // Exponents remain outside the profile. Rejecting them here keeps numeric
    // policy independent of the root or container consuming this value.
    if (position < source.get_size() &&
        (data[position] == 'e' || data[position] == 'E')) {
      return False;
    }

    value.set(negative ? -selected : selected);
    return True;
  }

  if (position < source.get_size() &&
      (data[position] == 'e' || data[position] == 'E')) {
    return False;
  }

  if (integer == signed_minimum_magnitude) {
    value.set(signed_minimum);
  } else {
    const S64 selected = S64(integer);
    value.set(negative ? -selected : selected);
  }

  return True;
}

static auto parse_json_value(
    Allocator::Arena& arena,
    View::Bytes source,
    U8* mutable_source,
    Count& position,
    Formats::Json::Node& value,
    Count depth) -> Bool {
  using Node = Formats::Json::Node;

  const U8* data = source.get_data();
  if (position == source.get_size()) {
    return False;
  }

  switch (data[position]) {
  case '{': {
    if (depth == Node::nesting_limit) {
      return False;
    }

    Managed::Vector<Node::Member> members(arena);
    position++;
    while (position < source.get_size()) {
      skip_json_whitespace(source, data, position, True);
      if (position < source.get_size() && data[position] == '}') {
        position++;
        value.set(members.get_view());
        return True;
      }

      if (position == source.get_size() || data[position] != '"') {
        return False;
      }

      View::Bytes name;
      if (!parse_json_string(arena, source, mutable_source, position, name)) {
        return False;
      }

      skip_json_whitespace(source, data, position);
      if (position == source.get_size() || data[position] != ':') {
        return False;
      }

      position++;
      skip_json_whitespace(source, data, position);

      Node& child = arena.construct<Node>();
      if (!parse_json_value(
              arena, source, mutable_source, position, child, depth + 1)) {
        return False;
      }

      members.insert(Node::Member(name, child));
    }

    return False;
  }

  case '[': {
    if (depth == Node::nesting_limit) {
      return False;
    }

    Managed::Vector<Node> values(arena);
    position++;
    while (position < source.get_size()) {
      skip_json_whitespace(source, data, position, True);
      if (position < source.get_size() && data[position] == ']') {
        position++;
        value.set(values.get_view());
        return True;
      }

      Node child;
      if (!parse_json_value(
              arena, source, mutable_source, position, child, depth + 1)) {
        return False;
      }

      values.insert(child);
    }

    return False;
  }

  case '"': {
    View::Bytes text;
    if (!parse_json_string(arena, source, mutable_source, position, text)) {
      return False;
    }

    value.set(text);
    return True;
  }

  case 't':
    if (source.slice(position, 4) != "true"_view) {
      return False;
    }

    position += 4;
    value.set(True);
    return True;

  case 'f':
    if (source.slice(position, 5) != "false"_view) {
      return False;
    }

    position += 5;
    value.set(False);
    return True;

  case 'n':
    if (source.slice(position, 4) != "null"_view) {
      return False;
    }

    position += 4;
    value.set();
    return True;

  case '-':
  case '0' ... '9':
    return parse_json_number(source, position, value);

  default:
    return False;
  }
}

static auto parse_json(
    Allocator::Arena& arena,
    View::Bytes source,
    U8* mutable_source = nullptr) -> Storage::Option<Formats::Json::Node> {
  const U8* data = source.get_data();
  Count position = 0;
  skip_json_whitespace(source, data, position);

  Formats::Json::Node value;
  if (!parse_json_value(arena, source, mutable_source, position, value, 0)) {
    return {};
  }

  // A parse publishes exactly one root. Relaxed container separators do not
  // permit a second value or arbitrary suffix after that root.
  skip_json_whitespace(source, data, position);
  if (position != source.get_size()) {
    return {};
  }

  return value;
}

auto Formats::Json::Node::parse(Allocator::Arena& arena, View::Bytes source)
    -> Storage::Option<Node> {
  return parse_json(arena, source);
}

auto Formats::Json::Node::parse(Managed::Bytes&& source)
    -> Storage::Option<Node> {
  Allocator::Arena& arena = source.get_arena();
  auto parsed = parse_json(arena, source.get_view(), source.get_data());
  source.reset();
  return parsed;
}

// Common control bytes use their short JSON escapes. Remaining C0 bytes use
// `\u00XX`, while every other byte except quote and backslash is copied without
// interpretation. This preserves arbitrary byte strings through formatting.
static auto write_json_string(
    Stream::Textual<Managed::Bytes>& stream,
    View::Bytes string) -> void {
  stream << "\""_view;

  constexpr auto hex_digits = "0123456789ABCDEF"_view;
  Count start = 0;
  for (Count i = 0; i < string.get_size(); i++) {
    View::Bytes escaped;
    switch (string[i]) {
    case '"':
      escaped = "\\\""_view;
      break;

    case '\\':
      escaped = "\\\\"_view;
      break;

    case '\b':
      escaped = "\\b"_view;
      break;

    case '\f':
      escaped = "\\f"_view;
      break;

    case '\n':
      escaped = "\\n"_view;
      break;

    case '\r':
      escaped = "\\r"_view;
      break;

    case '\t':
      escaped = "\\t"_view;
      break;

    default:
      if (string[i] < 0x20) {
        stream << string.slice(start, i - start);
        stream << "\\u00"_view;
        stream << hex_digits.slice(string[i] >> 4, 1);
        stream << hex_digits.slice(string[i] & 0x0F, 1);
        start = i + 1;
      }

      continue;
    }

    stream << string.slice(start, i - start);
    stream << escaped;
    start = i + 1;
  }

  stream << string.slice(start);
  stream << "\""_view;
}

auto Formats::Json::Node::format(Allocator::Arena& arena) const -> View::Bytes {
  Managed::Bytes output{arena};
  Stream::Textual<Managed::Bytes> stream(output);

  auto inplace_format =
      [](this auto&& self, Stream::Textual<Managed::Bytes>& stream,
         const Formats::Json::Node& node, Count depth) -> Bool {
    switch (State(node.data.state)) {
    case State::Null: {
      stream << "null"_view;
      return True;
    }

    case State::Array: {
      if (depth == nesting_limit) {
        return False;
      }

      stream << "["_view;

      View::Vector<Formats::Json::Node> array = node.get_array();
      for (U32 i = 0; i < array.get_size(); i++) {
        if (!self(stream, array.get_data()[i], depth + 1)) {
          return False;
        }

        if (i != array.get_size() - 1) {
          stream << ","_view;
        }
      }

      stream << "]"_view;
      return True;
    }

    case State::Object: {
      if (depth == nesting_limit) {
        return False;
      }

      stream << "{"_view;

      View::Vector<Member> members = node.get_object();
      for (U32 i = 0; i < members.get_size(); i++) {
        const auto& member = members.get_data()[i];
        write_json_string(stream, member.name);
        stream << ":"_view;
        if (!self(stream, member.node, depth + 1)) {
          return False;
        }

        if (i != members.get_size() - 1) {
          stream << ","_view;
        }
      }

      stream << "}"_view;
      return True;
    }

    case State::String: {
      write_json_string(stream, node.get_string());
      return True;
    }

    case State::Boolean: {
      stream << node.data.boolean;
      return True;
    }

    case State::Integer: {
      stream << node.data.integer;
      return True;
    }

    case State::Real: {
      constexpr R64 signed_limit = 9'223'372'036'854'775'808.0;
      if (!__builtin_isfinite(node.data.real) ||
          node.data.real >= signed_limit || node.data.real < -signed_limit) {
        return False;
      }

      stream << node.data.real;
      return True;
    }
    }

    __builtin_unreachable();
  };

  if (!inplace_format(stream, *this, 0)) {
    return {};
  }

  return output.get_view();
}
