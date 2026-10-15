// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/memory/allocator/global.hpp"
#include "perimortem/memory/container/bytes.hpp"

namespace Perimortem::Memory::Global {

// Global Bytes uses the process heap so allocation and release can occur on
// different threads. This supports explicit transfer across thread boundaries
// at the cost of the process allocator's synchronization. The policy does not
// make concurrent access to one Bytes object safe, so sharing data across
// threads still requires proper locking.
//
// Default construction uses the capability's empty representation.
class Bytes final : public Container::Bytes<Bytes> {
  using Base = Container::Bytes<Bytes>;
  friend Base;

 public:
  constexpr Bytes() = default;
  // Capacity construction can allocate without changing size.
  // Explicit construction prevents an integer from silently reserving storage.
  explicit Bytes(Count reserved_capacity) {
    Base::ensure_capacity(reserved_capacity);
  }

  // The caller keeps the View storage valid until construction returns. The
  // Bytes object copies those values into independent storage that it owns.
  // Explicit construction exposes the possible allocation at the call site.
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
  // The process heap publishes the requested extent exactly. Geometric growth
  // amortizes allocation and copying across repeated appends.
  static constexpr auto next_capacity(Count current, Count required) -> Count {
    constexpr Count initial_capacity = 32;
    const Count doubled = current > Count(-1) / 2 ? Count(-1) : current * 2;
    const Count grown = doubled > initial_capacity ? doubled : initial_capacity;
    return grown > required ? grown : required;
  }

  static auto allocate(Count requested_bytes) -> Allocation<U8> {
    return Allocator::Global::allocate(requested_bytes);
  }

  static auto release(U8* data) -> void { Allocator::Global::release(data); }
};

}  // namespace Perimortem::Memory::Global
