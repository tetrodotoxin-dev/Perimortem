// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/data.hpp"
#include "perimortem/memory/allocator/compiler.hpp"

namespace Perimortem::Memory::Const {

// Owns one value allocated through Compiler during constant evaluation. Copies
// create independent values and moves transfer the allocation.
template <typename type>
class Object {
 public:
  consteval Object() = default;
  constexpr ~Object() { destruct(); }

  consteval Object(const type& value) {
    construct();
    *source_block = value;
  }

  consteval Object(const Object& rhs) {
    if (rhs.source_block) {
      construct();
      *source_block = *rhs.source_block;
    }
  }

  consteval Object(Object&& rhs) {
    Core::Data::swap(source_block, rhs.source_block);
  }

  consteval auto operator=(const Object& rhs) -> Object& {
    // Noop
    if (source_block == rhs.source_block) {
      return *this;
    }

    // Requesting a clear.
    if (!rhs.source_block) {
      destruct();
      return *this;
    }

    // Since stroage is requested then make sure we have storage.
    construct();
    *source_block = *rhs.source_block;
    return *this;
  }

  consteval auto operator=(Object&& rhs) -> Object& {
    Core::Data::swap(source_block, rhs.source_block);
    return *this;
  }

  consteval auto get_data() const -> const type* { return source_block; }
  consteval auto get_data() -> type* { return source_block; }

 private:
  consteval auto construct() -> void {
    if (!source_block) {
      source_block = Allocator::Compiler::allocate_object<type>();
    }
  }

  constexpr auto destruct() -> void {
    if (source_block) {
      Allocator::Compiler::release_object(source_block);
      source_block = nullptr;
    }
  }

  type* source_block = nullptr;
};

}  // namespace Perimortem::Memory::Const
