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

static auto hex_value(U8 value) -> S32 {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }

  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }

  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }

  return -1;
}

static auto read_hex_quad(View::Bytes source, Count position, U32& value)
    -> Bool {
  if (position + 4 > source.get_size()) {
    return False;
  }

  value = 0;
  for (Count i = 0; i < 4; i++) {
    S32 digit = hex_value(source[position + i]);
    if (digit < 0) {
      return False;
    }

    value = (value << 4) | U32(digit);
  }

  return True;
}

static auto is_high_surrogate(U32 value) -> Bool {
  return value >= 0xD800 && value <= 0xDBFF;
}

static auto is_low_surrogate(U32 value) -> Bool {
  return value >= 0xDC00 && value <= 0xDFFF;
}

static auto append_utf8(Managed::Bytes& output, U32 codepoint) -> void {
  if (codepoint <= 0x7F) {
    output.append(U8(codepoint));
    return;
  }

  if (codepoint <= 0x7FF) {
    output.append(U8(0xC0 | (codepoint >> 6)));
    output.append(U8(0x80 | (codepoint & 0x3F)));
    return;
  }

  if (codepoint <= 0xFFFF) {
    output.append(U8(0xE0 | (codepoint >> 12)));
    output.append(U8(0x80 | ((codepoint >> 6) & 0x3F)));
    output.append(U8(0x80 | (codepoint & 0x3F)));
    return;
  }

  output.append(U8(0xF0 | (codepoint >> 18)));
  output.append(U8(0x80 | ((codepoint >> 12) & 0x3F)));
  output.append(U8(0x80 | ((codepoint >> 6) & 0x3F)));
  output.append(U8(0x80 | (codepoint & 0x3F)));
}

static auto read_unicode_escape(
    View::Bytes source,
    Count slash,
    U32& codepoint,
    Count& consumed) -> Bool {
  if (slash + 6 > source.get_size() || source[slash] != '\\' ||
      source[slash + 1] != 'u') {
    return False;
  }

  U32 first = 0;
  if (!read_hex_quad(source, slash + 2, first)) {
    return False;
  }

  if (!is_high_surrogate(first)) {
    if (is_low_surrogate(first)) {
      return False;
    }

    codepoint = first;
    consumed = 6;
    return True;
  }

  if (slash + 12 > source.get_size() || source[slash + 6] != '\\' ||
      source[slash + 7] != 'u') {
    return False;
  }

  U32 second = 0;
  if (!read_hex_quad(source, slash + 8, second) || !is_low_surrogate(second)) {
    return False;
  }

  codepoint = 0x10000 + ((first - 0xD800) << 10) + (second - 0xDC00);
  consumed = 12;
  return True;
}

// Scans a string byte by byte so ascii escape characters are skipped.
auto parse_string(View::Bytes source, Count& position) -> View::Bytes {
  const auto start = ++position;
  auto data = source.get_data();
  while (position < source.get_size()) {
    switch (data[position]) {
    case '"': {
      auto result = source.slice(start, position - start);
      position++;
      return result;
    }

    case '\\':
      position += 2;
      break;
    default:
      position++;
      break;
    }
  }

  return source.slice(start);
}

auto ignored_characters(U8 c) -> Bool {
  return c == ',' || c == '\n' || c == ' ';
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

auto Formats::Json::Node::decode_string(Allocator::Arena& arena) const
    -> View::Bytes {
  View::Bytes source = get_string();
  Managed::Bytes decoded{arena};
  for (Count i = 0; i < source.get_size(); i++) {
    if (source[i] != '\\' || i + 1 >= source.get_size()) {
      decoded.append(source[i]);
      continue;
    }

    Count slash = i;
    i++;
    switch (source[i]) {
    case 'n':
      decoded.append('\n');
      break;
    case 'r':
      decoded.append('\r');
      break;
    case 't':
      decoded.append('\t');
      break;
    case '"':
      decoded.append('"');
      break;
    case '\\':
      decoded.append('\\');
      break;
    case '/':
      decoded.append('/');
      break;
    case 'b':
      decoded.append('\b');
      break;
    case 'f':
      decoded.append('\f');
      break;
    case 'u': {
      U32 codepoint = 0;
      Count consumed = 0;
      if (read_unicode_escape(source, slash, codepoint, consumed)) {
        append_utf8(decoded, codepoint);
        i = slash + consumed - 1;
        break;
      }

      decoded.append('\\');
      decoded.append(source[i]);
      break;
    }

    default:
      decoded.append('\\');
      decoded.append(source[i]);
      break;
    }
  }

  return decoded.get_view();
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

auto Formats::Json::Node::parse(
    Allocator::Arena& arena,
    View::Bytes source,
    Count position) -> Count {
  if (position > source.get_size()) {
    set();
    return position;
  }

  while (position < source.get_size()) {
    const auto start_char = source[position];
    switch (start_char) {
    case '{': {
      Managed::Vector<Member> members(arena);
      Bool closed = False;
      position++;
      while (position < source.get_size()) {
        if (source[position] != '"') {
          if (source[position++] == '}') {
            closed = True;
            break;
          }

          continue;
        }

        auto name = parse_string(source, position);

        position++;

        // A child failure invalidates the containing object. Propagating the
        // sentinel prevents a partially parsed member list from being
        // published.
        Formats::Json::Node& child = arena.construct<Formats::Json::Node>();
        position = child.parse(arena, source, position);
        if (position == Count(-1)) {
          set();
          return Count(-1);
        }

        members.insert(Member(name, child));
      }

      // Publish collected members only after observing the closing brace.
      // Truncated input leaves this node null and propagates failure.
      if (!closed) {
        set();
        return Count(-1);
      }

      set(members);
      return position;
    }

    // Array
    case '[': {
      Managed::Vector<Formats::Json::Node> array(arena);
      position++;
      while (position < source.get_size()) {
        while (position < source.get_size() &&
               ignored_characters(source[position])) {
          position++;
        }

        if (position >= source.get_size()) {
          set();
          return Count(-1);
        }

        if (source[position] == ']') {
          break;
        }

        // A child failure invalidates the containing array. Propagating the
        // sentinel prevents its partial value list from being published.
        Formats::Json::Node& child = arena.construct<Formats::Json::Node>();
        position = child.parse(arena, source, position);
        if (position == Count(-1)) {
          set();
          return Count(-1);
        }

        array.insert(child);
      }

      if (position >= source.get_size()) {
        set();
        return Count(-1);
      }

      // Publish collected elements only after observing the closing bracket.
      // Truncated input leaves this node null and propagates failure.
      set(array.get_view());
      position++;
      return position;
    }

    // String
    case '"': {
      auto name = parse_string(source, position);
      set(name);
      return position;
    }

    // true
    case 't': {
      constexpr auto true_view = "true"_view;
      if (source.slice(position, true_view.get_size()) != "true"_view) {
        set();
        return Count(-1);
      }

      position += true_view.get_size();

      set(True);
      return position;
    }

    // false
    case 'f': {
      constexpr auto false_view = "false"_view;
      if (source.slice(position, false_view.get_size()) != "false"_view) {
        set();
        return Count(-1);
      }

      position += false_view.get_size();

      set(False);
      return position;
    }

    // Numbers
    case '-':
    case '0' ... '9': {
      Bool positive = True;
      if (source[position] == '-') {
        positive = False;
        position++;
      }

      S64 value = 0;
      while (position < source.get_size() && source[position] >= '0' &&
             source[position] <= '9') {
        value *= 10;
        value += source[position] - '0';
        position++;
      }

      if (position >= source.get_size() || source[position] != '.') {
        set(value * positive.sign());
        return position;
      }

      position++;
      R64 float_value = value;
      U64 divisor = 1;

      // Accumulate fractional digits and their decimal divisor together. One
      // division then places the accumulated digits after the radix point.
      while (position < source.get_size() && source[position] >= '0' &&
             source[position] <= '9') {
        float_value *= 10;
        divisor *= 10;
        float_value += source[position] - '0';
        position++;
      }

      set((float_value / R64(divisor)) * positive.sign());
      return position;
    }

    // null
    case 'n': {
      // JSON null uses the empty Node representation. Only the complete literal
      // advances the cursor successfully. A mismatch returns the failure
      // sentinel while leaving that same representation selected.
      set();

      constexpr auto null_view = "null"_view;
      if (source.slice(position, null_view.get_size()) != null_view) {
        return Count(-1);
      }

      position += null_view.get_size();
      return position;
    }

    default:
      position++;
      break;
    }
  }

  set();
  return position;
}

auto Formats::Json::Node::format(Allocator::Arena& arena) const -> View::Bytes {
  Managed::Bytes output{arena};
  Stream::Textual<Managed::Bytes> stream(output);

  auto inplace_format = [](this auto&& self,
                           Stream::Textual<Managed::Bytes>& stream,
                           const Formats::Json::Node& node) -> void {
    switch (State(node.data.state)) {
    case State::Null: {
      stream << "null"_view;
      return;
    }

    case State::Array: {
      stream << "["_view;

      View::Vector<Formats::Json::Node> array = node.get_array();
      for (U32 i = 0; i < array.get_size(); i++) {
        self(stream, array.get_data()[i]);
        if (i != array.get_size() - 1) {
          stream << ","_view;
        }
      }

      stream << "]"_view;
      return;
    }

    case State::Object: {
      stream << "{"_view;

      View::Vector<Member> members = node.get_object();
      for (U32 i = 0; i < members.get_size(); i++) {
        const auto& member = members.get_data()[i];
        stream << "\""_view << member.name << "\":"_view;

        self(stream, member.node);
        if (i != members.get_size() - 1) {
          stream << ","_view;
        }
      }

      stream << "}"_view;
      return;
    }

    case State::String: {
      stream << "\""_view;
      constexpr auto hex_digits = "0123456789ABCDEF"_view;
      auto string = node.get_string();
      for (Count i = 0; i < string.get_size(); i++) {
        switch (string[i]) {
        case '"':
          stream << "\\\""_view;
          break;

        case '\\':
          stream << "\\\\"_view;
          break;

        case '\b':
          stream << "\\b"_view;
          break;

        case '\f':
          stream << "\\f"_view;
          break;

        case '\n':
          stream << "\\n"_view;
          break;

        case '\r':
          stream << "\\r"_view;
          break;

        case '\t':
          stream << "\\t"_view;
          break;

        default:
          if (string[i] >= 0x20) {
            stream << string.slice(i, 1);
            break;
          }

          stream << "\\u00"_view;
          stream << hex_digits.slice(string[i] >> 4, 1);
          stream << hex_digits.slice(string[i] & 0x0F, 1);
          break;
        }
      }

      stream << "\""_view;
      return;
    }

    case State::Boolean: {
      stream << node.data.boolean;
      return;
    }

    case State::Integer: {
      stream << node.data.integer;
      return;
    }

    case State::Real: {
      stream << node.data.real;
      return;
    }
    }
  };

  inplace_format(stream, *this);

  return output;
}
