// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/memory/lifetime.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/memory/allocation.hpp"
#include "perimortem/memory/allocator/global.hpp"
#include "perimortem/memory/container/vector.hpp"

namespace Perimortem::Memory::Global {

// Global Vector uses the process heap so allocation and release can occur on
// different threads. This supports explicit transfer across thread boundaries
// at the cost of the process allocator's synchronization. The policy does not
// make concurrent access to one Vector safe, so sharing values across threads
// still requires proper locking. Each value must independently permit access
// and destruction in the receiving thread.
//
// Default construction uses the capability's empty representation.
template <typename type>
class Vector final : public Container::Vector<Vector<type>, type> {
  using Base = Container::Vector<Vector<type>, type>;
  friend Base;

 public:
  constexpr Vector() = default;
  // Capacity construction can allocate without changing size or creating
  // values. Explicit construction prevents an integer from silently reserving
  // storage.
  explicit Vector(Count reserved_capacity) {
    Base::ensure_capacity(reserved_capacity);
  }

  Vector(const Vector& rhs) { Base::copy_from(rhs); }

  // The caller keeps the View storage valid until construction returns. Vector
  // copies those values into a new process heap block. Explicit construction
  // exposes the possible allocation at the call site.
  explicit Vector(Core::View::Vector<type> source)
    requires(__is_constructible(type, const type&))
  {
    Base::copy_from(source);
  }

  constexpr Vector(Vector&& rhs) { Base::take(Core::Data::take(rhs)); }

  constexpr ~Vector() { Base::reset(); }

  auto operator=(const Vector& rhs) -> Vector& {
    if (this != &rhs) {
      Base::clear();
      Base::copy_from(rhs);
    }

    return *this;
  }

  auto operator=(Vector&& rhs) -> Vector& {
    if (this != &rhs) {
      Base::reset();
      Base::take(Core::Data::take(rhs));
    }

    return *this;
  }

  auto operator=(Core::View::Vector<type> source) -> Vector&
    requires(__is_constructible(type, const type&))
  {
    // Materialize the borrowed values before releasing this block so `source`
    // may refer to this Vector.
    Vector replacement(source);
    *this = Core::Data::take(replacement);
    return *this;
  }

 private:
  // The process heap publishes the requested extent exactly. Geometric growth
  // amortizes allocation and value relocation across repeated insertions.
  static constexpr auto next_capacity(Count current, Count required) -> Count {
    constexpr Count initial_capacity = 32;
    const Count doubled = current > Count(-1) / 2 ? Count(-1) : current * 2;
    const Count grown = doubled > initial_capacity ? doubled : initial_capacity;
    return grown > required ? grown : required;
  }

  static auto allocate(Count requested_capacity) -> Allocation<type> {
    static_assert(alignof(type) <= Allocator::Global::allocation_alignment);
    auto allocation =
        Allocator::Global::allocate(Base::allocation_size(requested_capacity));
    return Allocation<type>(
        Core::Data::cast<type>(allocation.get_data()),
        allocation.get_capacity() / sizeof(type));
  }

  static constexpr auto adopt(Allocation<type>&&) -> void {}

  template <typename factory_type>
  static constexpr auto construct(type* destination, factory_type factory)
      -> type& {
    return Toolchain::Memory::Lifetime::construct<type>(
        destination, Core::Data::take(factory));
  }

  // Growth constructs each current value in uninitialized storage at
  // `destination`. `source` remains alive in the old process heap block until
  // migration completes. Move construction completes before that block is
  // released. A value that cannot move is copied without changing `source`.
  static constexpr auto relocate(type* destination, type& source) -> void {
    if constexpr (__is_constructible(type, type&&)) {
      construct(
          destination, [&source] { return type(Core::Data::take(source)); });
    } else {
      construct(destination, [&source] { return type(source); });
    }
  }

  // Reusing an occupied address requires the former value lifetime to end
  // before the replacement is constructed at `destination`.
  static constexpr auto replace(type* destination, type& source) -> void {
    retire(destination, 1);
    relocate(destination, source);
  }

  // The process heap manages raw blocks and cannot end C++ value lifetimes.
  // Retire every live value before its storage is released or reused.
  static constexpr auto retire(type* data, Count size) -> void {
    if constexpr (!__is_trivially_destructible(type)) {
      for (Count index = 0; index < size; index++) {
        data[index].~type();
      }
    }
  }

  // A process heap block is released only after every value in it is retired.
  static auto release(type* data, Count size) -> void {
    retire(data, size);
    Allocator::Global::release(Core::Data::cast<U8>(data));
  }
};

}  // namespace Perimortem::Memory::Global
