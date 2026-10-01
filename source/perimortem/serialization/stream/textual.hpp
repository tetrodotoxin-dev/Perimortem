// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/view/bytes.hpp"

namespace Perimortem::Serialization::Stream {

// Appends Core::Writer::Textual output to Dynamic::Bytes or Managed::Bytes.
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

 private:
  storage_type& storage;
};

}  // namespace Perimortem::Serialization::Stream
