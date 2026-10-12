// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/memory/allocation.hpp"
#include "perimortem/memory/allocator/global.hpp"
#include "perimortem/memory/container/vector.hpp"

namespace Perimortem::Memory::Global {

// Global Vector owns values in process heap storage that can be released from
// any thread. This supports explicit transfer across thread boundaries at the
// cost of process allocator synchronization. Concurrent access to one vector
// still requires synchronization. Each element must independently permit
// access and destruction in the receiving thread.
template <typename type>
class Vector final : public Container::Vector<Vector<type>, type> {
  using Base = Container::Vector<Vector<type>, type>;
  friend Base;

 public:
  constexpr Vector() = default;
  // Capacity construction allocates without creating elements. Explicit
  // construction prevents an integer from silently reserving storage.
  explicit Vector(Count reserved_capacity) {
    Base::reset_storage(reserved_capacity);
  }

  Vector(const Vector& rhs) { Base::copy_from(rhs); }

  constexpr Vector(Vector&& rhs) { Base::take(static_cast<Base&&>(rhs)); }

  constexpr ~Vector() { Base::reset_storage(); }

  auto operator=(const Vector& rhs) -> Vector& {
    if (this != &rhs) {
      Base::clear();
      Base::copy_from(rhs);
    }

    return *this;
  }

  auto operator=(Vector&& rhs) -> Vector& {
    if (this != &rhs) {
      Base::reset_storage();
      Base::take(static_cast<Base&&>(rhs));
    }

    return *this;
  }

  auto reset() -> void { Base::reset_storage(); }

 private:
  // The process heap publishes the requested extent exactly. Geometric growth
  // amortizes allocation and element relocation across repeated insertions.
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
  static constexpr auto construct(type* destination, factory_type&& factory)
      -> type& {
    return Toolchain::Memory::Lifetime::construct<type>(
        destination, static_cast<factory_type&&>(factory));
  }

  // The process heap block is released immediately after relocation. Moving
  // can therefore transfer an available owner without preserving its old state.
  // A type that cannot be constructed from an rvalue falls back to copying.
  static constexpr auto relocate(type* destination, type& source) -> void {
    if constexpr (__is_constructible(type, type&&)) {
      construct(
          destination, [&source] { return type(static_cast<type&&>(source)); });
    } else {
      construct(destination, [&source] { return type(source); });
    }
  }

  static constexpr auto replace(type* destination, type& source) -> void {
    retire(destination, 1);
    relocate(destination, source);
  }

  static constexpr auto retire(type* data, Count size) -> void {
    if constexpr (!__is_trivially_destructible(type)) {
      for (Count index = 0; index < size; index++) {
        data[index].~type();
      }
    }
  }

  static auto release(type* data, Count size) -> void {
    retire(data, size);
    Allocator::Global::release(Core::Data::cast<U8>(data));
  }
};

}  // namespace Perimortem::Memory::Global
