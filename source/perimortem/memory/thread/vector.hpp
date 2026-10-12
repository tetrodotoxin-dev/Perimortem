// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/memory/allocation.hpp"
#include "perimortem/memory/allocator/bibliotheca.hpp"
#include "perimortem/memory/container/vector.hpp"

namespace Perimortem::Memory::Thread {

// Thread Vector owns values in the current thread's Bibliotheca archive. Every
// element lifetime ends before its block is remitted. The vector remains in its
// allocating thread through access, growth and destruction. Crossing a thread
// boundary requires copying its values into Global storage.
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

  // Changes size without preserving the allocation or its existing values.
  // Retaining a block above half capacity avoids Bibliotheca checkout churn.
  // Crossing capacity or falling to half requests a better fitting size class.
  constexpr auto forgetful_resize(Count required_size) -> void
    requires(
        __is_trivially_constructible(type) && __is_trivially_destructible(type))
  {
    const Count capacity = Base::get_capacity();
    if (required_size <= capacity && required_size > (capacity >> 1)) {
      Base::set_forgetful_size(required_size);
      return;
    }

    Base::reset_storage(required_size);
    Base::set_forgetful_size(required_size);
  }

 private:
  // Bibliotheca already rounds each request into a size class. Requesting the
  // exact requirement avoids applying a second growth policy.
  static constexpr auto next_capacity(Count, Count required) -> Count {
    return required;
  }

  static auto allocate(Count requested_capacity) -> Allocation<type> {
    static_assert(
        alignof(type) <= Allocator::Bibliotheca::allocation_alignment);
    auto allocation = Allocator::Bibliotheca::check_out(
        Base::allocation_size(requested_capacity));
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

  // Bibliotheca reclaims the source block immediately after relocation. Moving
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
    if (!data) {
      return;
    }

    retire(data, size);
    Allocator::Bibliotheca::remit(Core::Data::cast<U8>(data));
  }
};

}  // namespace Perimortem::Memory::Thread
