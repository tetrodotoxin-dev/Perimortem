// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/view/bytes.hpp"

namespace Perimortem::Serialization::Stream {

// Textual appends primitive representations and raw byte Views to Thread or
// Managed Bytes. Booleans, integers and Reals use the same representation as
// Core::Writer::Textual. Raw bytes append unchanged.
//
// Every append begins at the current size and can replace the destination
// allocation, invalidating earlier Views and Accesses into it. A nonfinite Real
// or one whose integer portion cannot fit S64 leaves its values and size
// unchanged and invalidates the stream. Later appends do no work.
template <typename storage_type>
class Textual {
 public:
  constexpr Textual(storage_type& storage) : storage(storage) {}

  EXPORTED(PERIMORTEM) auto operator<<(Bool flag) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(U8 byte) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(U16 value) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(U32 value) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(U64 value) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(S8 value) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(S16 value) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(S32 value) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(S64 value) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(R32 value) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(R64 value) -> Textual&;
  EXPORTED(PERIMORTEM) auto operator<<(Perimortem::Core::View::Bytes raw)
      -> Textual&;
  auto operator<<(const char* raw) -> Textual& = delete;

  constexpr auto is_valid() const -> Bool { return valid_state; }

 private:
  storage_type& storage;
  Bool valid_state = True;
};

}  // namespace Perimortem::Serialization::Stream
