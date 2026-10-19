// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/storage/option.hpp"
#include "perimortem/core/view/bytes.hpp"
#include "perimortem/core/view/vector.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/managed/bytes.hpp"

namespace Perimortem::Serialization::Formats::Json {

// Perimortem uses a JSON dialect for configuration and RPC data. Ordinary JSON
// values retain their familiar tree, but the following valid JSON forms are
// outside the deliberately bounded parser:
//
// 1. Numbers using exponent notation, more than 19 digits before the decimal
//    point, or an integer part outside the signed 64 bit range.
//
// 2. Unicode escapes other than `\u00XX`. That accepted form names one raw
//    byte, so values from `\u0080` through `\u00FF` are not converted to their
//    multi-byte UTF-8 representation. UTF-16 escape pairs are not interpreted.
//
// 3. More than 256 levels of Arrays or Objects.
//
// Perimortem also accepts the following forms outside the ordinary JSON
// grammar:
//
// 1. Array values and Object members may omit commas. Leading, repeated and
//    trailing commas are ignored, while the colon between a member name and
//    value remains required.
//
// 2. Every byte through ASCII space can separate values rather than only the
//    four JSON whitespace characters.
//
// 3. Quoted strings can contain arbitrary bytes, including raw control bytes
//    and byte sequences that are not valid UTF-8. `\u00XX` provides an escaped
//    spelling for any individual byte.
//
// 4. Numbers may contain leading zeroes or end immediately after their decimal
//    point.
//
// Inputs that avoid these differences produce the same value tree as ordinary
// JSON. Formatting emits required JSON punctuation and byte escapes without
// preserving optional source spelling. Systems requiring the complete JSON
// number or Unicode grammar use a different codec.
//
// Boolean, Integer and Real values live directly in the Node. Strings and
// containers use Views so every Node fits in sixteen bytes. Direct string and
// container construction retains caller storage rather than extending its
// lifetime.
//
// Indexing is total so callers can navigate an optional path without branching
// at every component. A mismatched container type, absent object name, or
// out of range array index returns a null Node and remains safe to index again.
// Typed getters follow the same data-oriented contract: the selected type
// returns its value, while every other type returns that value type's zero
// representation. Callers validating a schema inspect the corresponding
// `is_*` operation before extraction because zero is also valid JSON data.
class Node {
 public:
  // Parsing and formatting stop at the same recursion bound, preventing input
  // or a directly constructed cycle from consuming the native stack without
  // limit.
  static constexpr Count nesting_limit = 256;

  // A Member stores its name View and Node by reference so recursive Objects do
  // not enlarge Node. Direct construction keeps both caller values accessible
  // while the containing Object is used. Parsing stores Member records in the
  // selected Arena while each name follows the parse overload's source policy.
  // Blueprint copies both values into its destination Arena.
  class Member {
   public:
    constexpr Member(Core::View::Bytes name, const Node& node)
        : name(name), node(node) {}

    const Core::View::Bytes name{};
    const Node& node;
  };

  Node() : data{.ptr = nullptr, .size = 0, .state = 0} { set(); }
  Node(const Node& rhs) : data(rhs.data) {};
  Node(const Core::View::Bytes value)
      : data{.ptr = nullptr, .size = 0, .state = 0} {
    set(value);
  }

  Node(const Core::View::Vector<Node> value)
      : data{.ptr = nullptr, .size = 0, .state = 0} {
    set(value);
  }

  Node(const Core::View::Vector<Member> value)
      : data{.ptr = nullptr, .size = 0, .state = 0} {
    set(value);
  }

  Node(S64 value) : data{.ptr = nullptr, .size = 0, .state = 0} { set(value); }

  Node(R32 value) : data{.ptr = nullptr, .size = 0, .state = 0} {
    set(R64(value));
  }

  Node(R64 value) : data{.ptr = nullptr, .size = 0, .state = 0} { set(value); }

  Node(Bool value) : data{.ptr = nullptr, .size = 0, .state = 0} { set(value); }

  EXPORTED(PERIMORTEM) auto set(const Core::View::Bytes value) -> void;
  EXPORTED(PERIMORTEM) auto set(const Core::View::Vector<Node> value) -> void;
  EXPORTED(PERIMORTEM) auto set(const Core::View::Vector<Member> value) -> void;
  EXPORTED(PERIMORTEM) auto set(S64 value) -> void;
  EXPORTED(PERIMORTEM) auto set(R64 value) -> void;
  EXPORTED(PERIMORTEM) auto set(Bool value) -> void;
  EXPORTED(PERIMORTEM) auto set() -> void;

  // Array indexing returns a copied Node so later assignments do not mutate
  // the stored tree. Other JSON types and out of range indices return null.
  EXPORTED(PERIMORTEM) auto operator[](U32 index) const -> Node;

  // Object indexing searches authored member names. Other JSON types and
  // absent names return null, which keeps subsequent indexing valid.
  EXPORTED(PERIMORTEM) auto operator[](const Core::View::Bytes name) const
      -> Node;

  // Membership remains distinct from value extraction so an object member
  // whose authored value is null can be distinguished from an absent name.
  EXPORTED(PERIMORTEM) auto contains(const Core::View::Bytes name) const
      -> Bool;

  // Each getter returns the selected value or the return type's zero
  // representation when the Node contains another JSON type.
  EXPORTED(PERIMORTEM) auto get_boolean() const -> Bool;
  EXPORTED(PERIMORTEM) auto get_integer() const -> S64;
  EXPORTED(PERIMORTEM) auto get_real() const -> R64;
  EXPORTED(PERIMORTEM) auto get_string() const -> Core::View::Bytes;

  EXPORTED(PERIMORTEM) auto get_array() const -> Core::View::Vector<Node>;
  EXPORTED(PERIMORTEM) auto get_object() const -> Core::View::Vector<Member>;

  // Returns the number of bytes, array values, or object members stored by a
  // range type. Null, Boolean, Integer, and Real return zero.
  EXPORTED(PERIMORTEM) auto get_size() const -> Count;

  EXPORTED(PERIMORTEM) auto is_null() const -> Bool;
  EXPORTED(PERIMORTEM) auto is_boolean() const -> Bool;
  EXPORTED(PERIMORTEM) auto is_integer() const -> Bool;
  EXPORTED(PERIMORTEM) auto is_real() const -> Bool;
  EXPORTED(PERIMORTEM) auto is_string() const -> Bool;
  EXPORTED(PERIMORTEM) auto is_array() const -> Bool;
  EXPORTED(PERIMORTEM) auto is_object() const -> Bool;

  // Parses one JSON byte sequence into its corresponding Node tree. Unescaped
  // strings and member names can reference `source`, so its storage remains
  // accessible at the same address while the tree is used. Callers requiring
  // the Arena to retain those bytes copy them into Managed Bytes and use the
  // consuming overload. Allocations made before a late failure remain until
  // the Arena is reset.
  static EXPORTED(PERIMORTEM) auto parse(
      Memory::Allocator::Arena& arena,
      Core::View::Bytes source) -> Core::Storage::Option<Node>;

  // Parses and consumes JSON already stored in Managed Bytes. The tree reuses
  // that allocation and remains accessible for the Arena lifetime. The source
  // Bytes is empty after success or failure.
  static EXPORTED(PERIMORTEM) auto parse(Memory::Managed::Bytes&& source)
      -> Core::Storage::Option<Node>;

  // Serializes the complete tree into the supplied Arena using required JSON
  // punctuation and escapes. An empty View reports a nonfinite or out of range
  // Real, or a tree that reaches the recursion bound. Since an empty buffer is
  // not a JSON value, successful formatting is always distinguishable. The
  // source Node remains unchanged after failure. Bytes written before failure
  // remain in the Arena until reset.
  EXPORTED(PERIMORTEM) auto format(Memory::Allocator::Arena& arena) const
      -> Core::View::Bytes;

 private:
  enum class State : U32 {
    Null,
    String,
    Integer,
    Real,
    Object,
    Array,
    Boolean,
  };

  // An eight byte payload beside the U32 range size and state keeps Node at 16
  // bytes. Storage::Union places its discriminator after a 16 byte payload,
  // whose eight byte alignment rounds this alternative set to 24 bytes.
  struct {
    union {
      const void* ptr;
      S64 integer;
      R64 real;
      Bool boolean;
    };

    U32 size;
    U32 state;
  } data;
};

static_assert(sizeof(Node) == 16, "Size of Node is required to be 16 bytes.");

using Object = Core::View::Vector<Node::Member>;
using Array = Core::View::Vector<Node>;

}  // namespace Perimortem::Serialization::Formats::Json
