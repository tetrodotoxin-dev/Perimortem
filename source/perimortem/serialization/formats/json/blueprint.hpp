// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/data.hpp"
#include "perimortem/core/storage/union.hpp"
#include "perimortem/core/view/bytes.h"
#include "perimortem/core/view/vector.hpp"
#include "perimortem/serialization/formats/json/node.hpp"

namespace Perimortem::Serialization::Formats::Json {

// Blueprint describes a temporary JSON tree without allocating and materializes
// that complete tree in the caller's Arena. Container construction names array
// or object shape explicitly, so no child name or ordering selects the shape.
// Object children use their authored names, including an empty name. Array
// children use only their values. A selected shape remains intact when it has
// no children.
//
// Child descriptor ranges remain accessible until materialization completes.
// Materialization recursively copies string values, object member names, and an
// existing Node tree into the destination Arena. The resulting Node therefore
// depends only on that Arena, and resetting the Arena ends access to its
// complete tree. An existing Node must represent an acyclic JSON tree because
// every child is visited recursively.
class Blueprint {
 private:
  class Array {
   public:
    constexpr Array(Core::View::Vector<Blueprint> values) : values(values) {}

    constexpr auto get_values() const -> Core::View::Vector<Blueprint> {
      return values;
    }

   private:
    Core::View::Vector<Blueprint> values;
  };

  class Object {
   public:
    constexpr Object(Core::View::Vector<Blueprint> members)
        : members(members) {}

    constexpr auto get_members() const -> Core::View::Vector<Blueprint> {
      return members;
    }

   private:
    Core::View::Vector<Blueprint> members;
  };

  using Value = Core::Storage::
      Union<Core::View::Bytes, Array, Object, Node, S64, R64, Bool>;

 public:
  Blueprint() = default;
  Blueprint(Core::View::Bytes text) : value(text) {}
  // Normalize every nonboolean integer to the S64 alternative so visitors do
  // not distinguish source widths. Bool retains the JSON boolean alternative.
  template <typename Integer>
    requires(__is_integral(Integer) && !__is_same(Integer, bool))
  Blueprint(Integer number) : value(S64(number)) {}
  Blueprint(R64 real) : value(real) {}
  Blueprint(R32 real) : Blueprint(R64(real)) {}
  Blueprint(Bool flag) : value(flag) {}
  Blueprint(const Node& node) : value(node) {}

  Blueprint(Core::View::Bytes member_name, Core::View::Bytes text)
      : name(member_name), value(text) {}
  template <typename Integer>
    requires(__is_integral(Integer) && !__is_same(Integer, bool))
  Blueprint(Core::View::Bytes member_name, Integer number)
      : name(member_name), value(S64(number)) {}
  Blueprint(Core::View::Bytes member_name, R64 real)
      : name(member_name), value(real) {}
  Blueprint(Core::View::Bytes member_name, R32 real)
      : Blueprint(member_name, R64(real)) {}
  Blueprint(Core::View::Bytes member_name, Bool flag)
      : name(member_name), value(flag) {}
  Blueprint(Core::View::Bytes member_name, const Node& node)
      : name(member_name), value(node) {}
  Blueprint(Core::View::Bytes member_name, Blueprint child)
      : name(member_name), value(child.value) {}

  static constexpr auto array() -> Blueprint;
  static constexpr auto array(Core::View::Vector<Blueprint> values)
      -> Blueprint;

  template <Count N>
  static constexpr auto array(const Blueprint (&values)[N]) -> Blueprint {
    return array(Core::View::Vector<Blueprint>(values));
  }

  static constexpr auto object() -> Blueprint;
  static constexpr auto object(Core::View::Vector<Blueprint> members)
      -> Blueprint;

  template <Count N>
  static constexpr auto object(const Blueprint (&members)[N]) -> Blueprint {
    return object(Core::View::Vector<Blueprint>(members));
  }

  constexpr auto get_name() const -> Core::View::Bytes { return name; }

  // Copies every described value into the supplied Arena. Arrays and objects
  // retain their explicit factory shape, including when they contain no values.
  EXPORTED(PERIMORTEM) auto construct(Memory::Allocator::Arena& arena) const
      -> Node;

 private:
  constexpr Blueprint(Array array) : value(array) {}
  constexpr Blueprint(Object object) : value(object) {}

  Core::View::Bytes name{};
  Value value;
};

constexpr auto Blueprint::array() -> Blueprint {
  return array(Core::View::Vector<Blueprint>());
}

constexpr auto Blueprint::array(Core::View::Vector<Blueprint> values)
    -> Blueprint {
  return Blueprint(Array(values));
}

constexpr auto Blueprint::object() -> Blueprint {
  return object(Core::View::Vector<Blueprint>());
}

constexpr auto Blueprint::object(Core::View::Vector<Blueprint> members)
    -> Blueprint {
  return Blueprint(Object(members));
}

}  // namespace Perimortem::Serialization::Formats::Json
