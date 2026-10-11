// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.h"

namespace Perimortem::Memory::Allocator {

// Compiler owns the built in C++ allocation expressions. Constant evaluation
// requires these language operations, and Const containers retain the same
// lifetime model when evaluated at runtime.
class Compiler {
 public:
  template <typename Type>
  static constexpr auto allocate_object() -> Type* {
    return new Type{};
  }

  template <typename Type>
  static constexpr auto release_object(Type* address) -> void {
    delete address;
  }

  template <typename Type>
  static constexpr auto allocate_range(Count size) -> Type* {
    return new Type[size]{};
  }

  template <typename Type>
  static constexpr auto release_range(Type* address) -> void {
    delete[] address;
  }
};

}  // namespace Perimortem::Memory::Allocator
