// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/memory/allocation.hpp"
#include "perimortem/memory/allocator/bibliotheca.hpp"
#include "perimortem/memory/container/bytes.hpp"

namespace Perimortem::Memory::Thread {

// Thread Bytes uses the current thread's Bibliotheca archive. This avoids
// process wide allocation synchronization and reuses the archive's size class
// blocks. A value holding storage remains in its allocating thread through
// access, growth and destruction. Crossing a thread boundary requires copying
// the value into Global storage.
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
    Base::ensure_capacity(reserved_capacity);
  }

  // The caller keeps the View storage valid until construction returns. This
  // value copies those bytes into independent storage that it owns. Explicit
  // construction exposes the possible allocation at the call site.
  explicit Bytes(Core::View::Bytes view) { Base::copy(view); }

  Bytes(const Bytes& rhs) { Base::copy(rhs.get_view()); }

  constexpr Bytes(Bytes&& rhs) { Base::take(static_cast<Base&&>(rhs)); }

  constexpr ~Bytes() = default;

  auto operator=(Core::View::Bytes view) -> Bytes& {
    Base::copy(view);
    return *this;
  }

  auto operator=(const Bytes& rhs) -> Bytes& {
    if (this != &rhs) {
      Base::copy(rhs.get_view());
    }

    return *this;
  }

  auto operator=(Bytes&& rhs) -> Bytes& {
    if (this != &rhs) {
      Base::reset();
      Base::take(static_cast<Base&&>(rhs));
    }

    return *this;
  }

 private:
  // Bibliotheca already rounds requests into size class capacity. Requesting
  // the exact requirement avoids applying a second growth policy.
  static constexpr auto next_capacity(Count, Count required) -> Count {
    return required;
  }

  static auto allocate(Count requested_bytes) -> Allocation<U8> {
    return Allocator::Bibliotheca::check_out(requested_bytes);
  }

  static auto release(U8* data) -> void {
    if (data) {
      Allocator::Bibliotheca::remit(data);
    }
  }
};

}  // namespace Perimortem::Memory::Thread
