// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/memory/lifetime.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/memory/allocator/bibliotheca.hpp"
#include "perimortem/memory/container/set.hpp"

namespace Perimortem::Memory::Thread {

// Thread Set stores its control hashes and keys in the current thread's
// Bibliotheca archive. Every key lifetime ends before the allocation is
// remitted. The Set remains in its allocating thread through access, growth and
// destruction. Crossing a thread boundary requires copying its keys into
// Global storage.
template <typename key_type>
class Set final : public Container::Set<Set<key_type>, key_type> {
  using Base = Container::Set<Set<key_type>, key_type>;
  friend Base;

 public:
  constexpr Set() = default;
  // Capacity construction allocates without creating keys. Explicit
  // construction prevents an integer from silently reserving storage.
  explicit Set(Count reserved_capacity) {
    Base::ensure_capacity(reserved_capacity);
  }

  Set(const Set& rhs)
    requires(__is_constructible(key_type, const key_type&))
  {
    Base::copy_from(rhs);
  }

  constexpr Set(Set&& rhs) { Base::take(Core::Data::take(rhs)); }

  constexpr ~Set() { Base::reset_storage(); }

  auto operator=(const Set& rhs) -> Set&
    requires(__is_constructible(key_type, const key_type&))
  {
    if (this != &rhs) {
      Base::clear();
      Base::copy_from(rhs);
    }

    return *this;
  }

  auto operator=(Set&& rhs) -> Set& {
    if (this != &rhs) {
      Base::reset_storage();
      Base::take(Core::Data::take(rhs));
    }

    return *this;
  }

 private:
  static auto allocate(Count requested_bytes) -> Allocation<U8> {
    static_assert(
        alignof(key_type) <= Allocator::Bibliotheca::allocation_alignment);
    return Allocator::Bibliotheca::check_out(requested_bytes);
  }

  template <typename type, typename factory_type>
  static constexpr auto construct(type* destination, factory_type factory)
      -> type& {
    return Toolchain::Memory::Lifetime::construct<type>(
        destination, Core::Data::take(factory));
  }

  // Rehashing constructs each key in uninitialized storage at `destination`.
  // `source` remains alive in the old Bibliotheca allocation until migration
  // completes. Move construction completes before that allocation is remitted.
  // A key that cannot move is copied without changing `source`.
  template <typename type>
  static constexpr auto relocate(type* destination, type& source) -> void {
    if constexpr (__is_constructible(type, type&&)) {
      construct(
          destination, [&source] { return type(Core::Data::take(source)); });
    } else {
      construct(destination, [&source] { return type(source); });
    }
  }

  // Bibliotheca manages raw storage and cannot end key lifetimes.
  template <typename type>
  static constexpr auto retire(type* values, Count count) -> void {
    if constexpr (!__is_trivially_destructible(type)) {
      for (Count index = 0; index < count; index++) {
        values[index].~type();
      }
    }
  }

  // The table allocation returns to Bibliotheca after all keys are retired.
  static auto release(U8* data) -> void {
    if (data) {
      Allocator::Bibliotheca::remit(data);
    }
  }
};

}  // namespace Perimortem::Memory::Thread
