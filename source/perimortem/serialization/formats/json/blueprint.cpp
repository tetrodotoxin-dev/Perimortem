// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/serialization/formats/json/blueprint.hpp"

#include "perimortem/memory/managed/vector.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::Serialization;

auto Formats::Json::Blueprint::construct(Allocator::Arena& arena) const
    -> Formats::Json::Node {
  return visit(
      []() { return Formats::Json::Node(); },
      [](View::Bytes text) { return Formats::Json::Node(text); },
      [](S64 number) { return Formats::Json::Node(number); },
      [](R64 real) { return Formats::Json::Node(real); },
      [](Bool flag) { return Formats::Json::Node(flag); },
      [&](View::Vector<Formats::Json::Blueprint> compound) {
        const Bool is_object =
            !compound.is_empty() && !compound[0].get_name().is_empty();
        if (is_object) {
          Managed::Vector<Formats::Json::Node::Member> members(arena);
          for (Count i = 0; i < compound.get_size(); i++) {
            const auto& child = arena.construct<Formats::Json::Node>(
                compound[i].construct(arena));
            members.insert(
                Formats::Json::Node::Member(compound[i].get_name(), child));
          }

          return Formats::Json::Node(members);
        }

        Managed::Vector<Formats::Json::Node> nodes(arena);
        for (Count i = 0; i < compound.get_size(); i++) {
          nodes.insert(compound[i].construct(arena));
        }

        return Formats::Json::Node(nodes);
      },

      [](Formats::Json::Node node) { return node; });
}
