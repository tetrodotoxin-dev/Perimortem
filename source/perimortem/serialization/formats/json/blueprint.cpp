// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/serialization/formats/json/blueprint.hpp"

#include "perimortem/memory/managed/vector.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::Serialization;

// Existing Nodes can retain Views into another source buffer or Arena. Copying
// each range while recreating its shape makes Blueprint construction detach the
// resulting tree from every source lifetime.
static auto materialize_node(
    Allocator::Arena& arena,
    const Formats::Json::Node& source) -> Formats::Json::Node {
  if (source.is_boolean()) {
    return Formats::Json::Node(source.get_boolean());
  }

  if (source.is_integer()) {
    return Formats::Json::Node(source.get_integer());
  }

  if (source.is_real()) {
    return Formats::Json::Node(source.get_real());
  }

  if (source.is_string()) {
    return Formats::Json::Node(arena.copy(source.get_string()));
  }

  if (source.is_array()) {
    Managed::Vector<Formats::Json::Node> values(arena);
    const auto source_values = source.get_array();
    for (Count index = 0; index < source_values.get_size(); index++) {
      values.insert(materialize_node(arena, source_values.get_data()[index]));
    }

    return Formats::Json::Node(values);
  }

  if (source.is_object()) {
    Managed::Vector<Formats::Json::Node::Member> members(arena);
    const auto source_members = source.get_object();
    for (Count index = 0; index < source_members.get_size(); index++) {
      const auto& source_member = source_members.get_data()[index];
      const View::Bytes name = arena.copy(source_member.name);
      const auto& value = arena.construct<Formats::Json::Node>(
          materialize_node(arena, source_member.node));
      members.insert(Formats::Json::Node::Member(name, value));
    }

    return Formats::Json::Node(members);
  }

  return Formats::Json::Node();
}

auto Formats::Json::Blueprint::construct(Allocator::Arena& arena) const
    -> Formats::Json::Node {
  return value.visit(
      []() { return Formats::Json::Node(); },
      [&](View::Bytes text) { return Formats::Json::Node(arena.copy(text)); },

      [&](const Array& array) {
        Managed::Vector<Formats::Json::Node> values(arena);
        const auto source = array.get_values();
        for (Count index = 0; index < source.get_size(); index++) {
          values.insert(source.get_data()[index].construct(arena));
        }

        return Formats::Json::Node(values);
      },

      [&](const Object& object) {
        Managed::Vector<Formats::Json::Node::Member> members(arena);
        const auto source = object.get_members();
        for (Count index = 0; index < source.get_size(); index++) {
          const auto& member = source.get_data()[index];
          const View::Bytes name = arena.copy(member.get_name());
          const auto& child =
              arena.construct<Formats::Json::Node>(member.construct(arena));
          members.insert(Formats::Json::Node::Member(name, child));
        }

        return Formats::Json::Node(members);
      },

      [&](const Formats::Json::Node& node) {
        return materialize_node(arena, node);
      },

      [](S64 number) { return Formats::Json::Node(number); },
      [](R64 real) { return Formats::Json::Node(real); },
      [](Bool flag) { return Formats::Json::Node(flag); });
}
