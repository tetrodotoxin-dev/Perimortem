// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/memory/allocator/allocation.hpp"
#include "perimortem/memory/container/bytes.hpp"

namespace Perimortem::Memory::Global {

// Global Bytes uses the process heap so allocation and release can occur on
// different threads. This supports explicit transfer across thread boundaries
// at the cost of the process allocator's synchronization. The policy does not
// make concurrent access to one value safe, so sharing data across threads
// still requires proper locking.
//
// Default construction uses the capability's allocation free null state.
class Bytes final : public Container::Bytes<Bytes> {
  using Base = Container::Bytes<Bytes>;
  friend Base;

 public:
  constexpr Bytes() = default;
  // Capacity construction can allocate without producing visible bytes.
  // Explicit construction prevents an integer from silently reserving storage.
  explicit Bytes(Count reserved_capacity) {
    ensure_capacity(reserved_capacity);
  }

  // The caller keeps the View storage valid until construction returns. This
  // value copies those bytes into independent storage that it owns. Explicit
  // construction exposes the possible allocation at the call site.
  explicit Bytes(Core::View::Bytes view) { copy(view); }

  Bytes(const Bytes& rhs) { copy(rhs.get_view()); }

  constexpr Bytes(Bytes&& rhs) { take(static_cast<Base&&>(rhs)); }

  constexpr ~Bytes() = default;

  auto operator=(Core::View::Bytes view) -> Bytes& {
    copy(view);
    return *this;
  }

  auto operator=(const Bytes& rhs) -> Bytes& {
    if (this != &rhs) {
      copy(rhs.get_view());
    }

    return *this;
  }

  auto operator=(Bytes&& rhs) -> Bytes& {
    if (this != &rhs) {
      reset();
      take(static_cast<Base&&>(rhs));
    }

    return *this;
  }

 private:
  // The process heap publishes the requested extent exactly. Geometric growth
  // amortizes allocation and copying across repeated appends.
  static constexpr auto next_capacity(Count current, Count required) -> Count {
    return Allocator::geometric_capacity(current, required);
  }

  static EXPORTED(PERIMORTEM) auto allocate(Count requested_bytes)
      -> Allocator::Allocation;
  static EXPORTED(PERIMORTEM) auto release(U8* data) -> void;
};

}  // namespace Perimortem::Memory::Global
