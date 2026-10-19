// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/data.hpp"
#include "perimortem/core/view/bytes.hpp"
#include "perimortem/core/view/vector.hpp"

namespace Perimortem::Serialization::Stream {

// Binary appends packed scalar and vector values to Thread or Managed Bytes.
// Scalars and multi byte vector elements use the declared byte order. Byte
// Views and one byte elements retain their existing representation. No
// alignment padding is added.
//
// Every append begins at the current size and can replace the destination
// allocation, invalidating earlier Views and Accesses into it. Binary never
// revisits existing values. Use Core::Writer::Binary when output needs a fixed
// capacity or later patching.
template <
    Perimortem::Core::Data::ByteOrder stream_endian,
    typename storage_type>
class Binary {
 public:
  constexpr Binary(storage_type& storage) : storage(storage) {}

  EXPORTED(PERIMORTEM) auto operator<<(U8 value) -> Binary&;
  EXPORTED(PERIMORTEM) auto operator<<(U16 value) -> Binary&;
  EXPORTED(PERIMORTEM) auto operator<<(U32 value) -> Binary&;
  EXPORTED(PERIMORTEM) auto operator<<(U64 value) -> Binary&;
  EXPORTED(PERIMORTEM) auto operator<<(S8 value) -> Binary&;
  EXPORTED(PERIMORTEM) auto operator<<(S16 value) -> Binary&;
  EXPORTED(PERIMORTEM) auto operator<<(S32 value) -> Binary&;
  EXPORTED(PERIMORTEM) auto operator<<(S64 value) -> Binary&;
  EXPORTED(PERIMORTEM) auto operator<<(R32 value) -> Binary&;
  EXPORTED(PERIMORTEM) auto operator<<(R64 value) -> Binary&;
  EXPORTED(PERIMORTEM) auto operator<<(Perimortem::Core::View::Bytes blob)
      -> Binary&;
  EXPORTED(PERIMORTEM) auto operator<<(Perimortem::Core::View::Vector<U8> blob)
      -> Binary&;
  EXPORTED(PERIMORTEM) auto operator<<(Perimortem::Core::View::Vector<U16> blob)
      -> Binary&;
  EXPORTED(PERIMORTEM) auto operator<<(Perimortem::Core::View::Vector<U32> blob)
      -> Binary&;
  EXPORTED(PERIMORTEM) auto operator<<(Perimortem::Core::View::Vector<U64> blob)
      -> Binary&;
  EXPORTED(PERIMORTEM) auto operator<<(Perimortem::Core::View::Vector<S8> blob)
      -> Binary&;
  EXPORTED(PERIMORTEM) auto operator<<(Perimortem::Core::View::Vector<S16> blob)
      -> Binary&;
  EXPORTED(PERIMORTEM) auto operator<<(Perimortem::Core::View::Vector<S32> blob)
      -> Binary&;
  EXPORTED(PERIMORTEM) auto operator<<(Perimortem::Core::View::Vector<S64> blob)
      -> Binary&;

 private:
  storage_type& storage;
};

}  // namespace Perimortem::Serialization::Stream
