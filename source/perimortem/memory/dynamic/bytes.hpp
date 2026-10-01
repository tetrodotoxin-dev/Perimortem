// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/view/bytes.hpp"
#include "perimortem/core/access/bytes.hpp"
#include "perimortem/core/hash.hpp"

namespace Perimortem::Memory::Dynamic {

// A vector of dynamically managed bytes with value semantics. Copies allocate
// their own storage and writable access uses that storage directly. Each
// allocation uses the thread local Bibliotheca for allocation so transfers
// between threads requires a copy.
//
// Bytes is guaranteed to have an allocation free default constructor.
class Bytes {
 public:
  constexpr Bytes() = default;
  EXPORTED(PERIMORTEM) Bytes(Count reserved_capacity);
  EXPORTED(PERIMORTEM) Bytes(Core::View::Bytes view);
  EXPORTED(PERIMORTEM) Bytes(const Dynamic::Bytes& rhs);
  EXPORTED(PERIMORTEM) Bytes(Dynamic::Bytes&& rhs);

  EXPORTED(PERIMORTEM) auto operator=(Core::View::Bytes view)
      -> Dynamic::Bytes&;
  EXPORTED(PERIMORTEM) auto operator=(const Dynamic::Bytes& rhs)
      -> Dynamic::Bytes&;
  EXPORTED(PERIMORTEM) auto operator=(Dynamic::Bytes&& rhs) -> Dynamic::Bytes&;

  auto operator==(const Dynamic::Bytes& rhs) const -> Bool {
    return get_view() == rhs.get_view();
  }

  auto operator==(const Core::View::Bytes& rhs) const -> Bool {
    return get_view() == rhs;
  }

  EXPORTED(PERIMORTEM) ~Bytes();

  constexpr operator Core::View::Bytes() const { return get_view(); }
  constexpr operator Core::Access::Bytes() { return get_access(); }

  EXPORTED(PERIMORTEM) auto append(U8 byte) -> void;
  EXPORTED(PERIMORTEM) auto append(U8 byte, Count amount) -> void;
  EXPORTED(PERIMORTEM) auto concat(Core::View::Bytes view) -> void;
  EXPORTED(PERIMORTEM) auto proxy(Core::View::Bytes view) -> void;

  // Resizes the container but attempts to preserve as much of the original
  // buffer as will fit in the new size.
  //
  // Shrinking preserves the remaining buffer so restoring the old size can
  // recover its bytes. Callers initialize bytes obtained from new capacity.
  EXPORTED(PERIMORTEM) auto resize(Count new_size) -> void;
  // Ensures there is enough room to store a required size, but declares we
  // don't care about the buffer's existing contents.
  //
  // Both growing and shrinking the buffer can be destructive operations so the
  // contents after a forgetful operation should always be assumed to be in an
  // invalid state.
  EXPORTED(PERIMORTEM) auto forgetful_resize(Count required_size) -> void;

  // Shrinks the container from the front by a number of bytes.
  //
  // If the container is shrunk more than it's current size the call is
  // equivilant to a clear.
  EXPORTED(PERIMORTEM) auto shrink(Count bytes_to_remove) -> void;

  EXPORTED(PERIMORTEM) auto operator[](Count index) const -> U8;
  EXPORTED(PERIMORTEM) auto at(Count index) const -> U8;
  EXPORTED(PERIMORTEM) auto set(U8 target) -> void;
  EXPORTED(PERIMORTEM) auto convert(U8 source, U8 target) -> void;
  EXPORTED(PERIMORTEM) auto slice(Count start, Count size) const
      -> Core::View::Bytes;

  constexpr auto get_size() const -> Count { return size; }
  constexpr auto get_capacity() const -> Count { return capacity; }
  constexpr auto get_view() const -> Core::View::Bytes {
    return Core::View::Bytes(source_block, size);
  }

  constexpr auto get_access() -> Core::Access::Bytes {
    return Core::Access::Bytes(source_block, size);
  }

  auto hash() const -> U64 { return Core::Hash(get_view()).get_value(); }

  constexpr auto is_empty() const -> Bool { return size == 0; }

  EXPORTED(PERIMORTEM) auto clear() -> void;
  EXPORTED(PERIMORTEM) auto reset() -> void;
  EXPORTED(PERIMORTEM) auto ensure_capacity(Count required_size) -> void;

 private:
  U8* source_block = nullptr;
  Count size = 0;
  Count capacity = 0;
};

}  // namespace Perimortem::Memory::Dynamic
