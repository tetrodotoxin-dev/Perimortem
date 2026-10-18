// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/view/bytes.h"
#include "perimortem/core/view/vector.hpp"
#include "perimortem/memory/allocator/arena.hpp"

namespace Perimortem::Serialization::Formats::Json {

// Node stores Boolean, Integer, and Real JSON values directly. Strings, arrays,
// objects, and member names retain Views into caller storage so every Node fits
// in sixteen bytes. Parsing materializes array values, object members, and
// child Nodes in the supplied Arena, while parsed strings and member names
// continue to view the source text. The source text and Arena remain accessible
// for the lifetime of each copied Node.
//
// Indexing is total so callers can navigate an optional path without branching
// at every component. A mismatched container type, absent object name, or
// out-of-range array index returns a null Node and remains safe to index again.
// Typed getters follow the same data-oriented contract: the selected type
// returns its value, while every other type returns that value type's zero
// representation. Callers validating a schema inspect the corresponding
// `is_*` operation before extraction because zero is also valid JSON data.
class Node {
 public:
  // Object membership is recursive, so a Member retains its materialized Node
  // by reference. Both the Member range and every referred Node share the
  // caller's arena lifetime.
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
  // the stored tree. Other JSON types and out-of-range indices return null.
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

  // Decodes JSON escape sequences into the supplied Arena. A Node containing
  // another JSON type returns an allocation free empty View.
  EXPORTED(PERIMORTEM) auto decode_string(Memory::Allocator::Arena& arena) const
      -> Core::View::Bytes;
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

  EXPORTED(PERIMORTEM) auto parse(
      Memory::Allocator::Arena& arena,
      Core::View::Bytes source,
      Count position = 0) -> Count;
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

  // Node deliberately packs its scalar payload, range size, and state into 16
  // bytes. Storage::Union would require separate storage for its tag and since
  // C++ can't unpack the struct it will tack it on to the end with padding.
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
