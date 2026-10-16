// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/memory/lifetime.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/memory/allocator/global.hpp"
#include "perimortem/memory/container/map.hpp"

namespace Perimortem::Memory::Global {

// Global Map stores its control hashes, keys and values in process heap storage
// that can be released from any thread. This supports explicit transfer across
// thread boundaries at the cost of process allocator synchronization.
// Concurrent access still requires synchronization. Every key and value must
// independently permit access and destruction in the receiving thread.
template <typename key_type, typename value_type>
class Map final
    : public Container::Map<Map<key_type, value_type>, key_type, value_type> {
  using Base = Container::Map<Map<key_type, value_type>, key_type, value_type>;
  friend Base;

 public:
  constexpr Map() = default;
  // Capacity construction allocates without creating key value pairs. Explicit
  // construction prevents an integer from silently reserving storage.
  explicit Map(Count reserved_capacity) {
    Base::ensure_capacity(reserved_capacity);
  }

  Map(const Map& rhs)
    requires(
        __is_constructible(key_type, const key_type&) &&
        __is_constructible(value_type, const value_type&))
  {
    Base::copy_from(rhs);
  }

  constexpr Map(Map&& rhs) { Base::take(Core::Data::take(rhs)); }

  constexpr ~Map() { Base::reset_storage(); }

  auto operator=(const Map& rhs) -> Map&
    requires(
        __is_constructible(key_type, const key_type&) &&
        __is_constructible(value_type, const value_type&))
  {
    if (this != &rhs) {
      Base::clear();
      Base::copy_from(rhs);
    }

    return *this;
  }

  auto operator=(Map&& rhs) -> Map& {
    if (this != &rhs) {
      Base::reset_storage();
      Base::take(Core::Data::take(rhs));
    }

    return *this;
  }

 private:
  static auto allocate(Count requested_bytes) -> Allocation<U8> {
    static_assert(
        alignof(typename Base::Bucket) <=
        Allocator::Global::allocation_alignment);
    return Allocator::Global::allocate(requested_bytes);
  }

  template <typename type, typename factory_type>
  static constexpr auto construct(type* destination, factory_type factory)
      -> type& {
    return Toolchain::Memory::Lifetime::construct<type>(
        destination, Core::Data::take(factory));
  }

  // Rehashing constructs each key value pair in uninitialized storage at
  // `destination`. `source` remains alive in the old process heap allocation
  // until migration completes. Move construction completes before that
  // allocation is released. A key value pair that cannot move is copied without
  // changing `source`.
  template <typename type>
  static constexpr auto relocate(type* destination, type& source) -> void {
    if constexpr (__is_constructible(type, type&&)) {
      construct(
          destination, [&source] { return type(Core::Data::take(source)); });
    } else {
      construct(destination, [&source] { return type(source); });
    }
  }

  // The process heap manages raw storage and cannot end key or value lifetimes.
  template <typename type>
  static constexpr auto retire(type* values, Count count) -> void {
    if constexpr (!__is_trivially_destructible(type)) {
      for (Count index = 0; index < count; index++) {
        values[index].~type();
      }
    }
  }

  // Assignment materializes the replacement before ending the current value
  // lifetime. This keeps a source derived from the current value accessible
  // until construction of its replacement has completed.
  template <typename type, typename factory_type>
  static constexpr auto replace(type* destination, factory_type factory)
      -> type& {
    type candidate = factory();
    retire(destination, 1);
    relocate(destination, candidate);
    return *destination;
  }

  // The table allocation is released after all key value pairs are retired.
  static auto release(U8* data) -> void { Allocator::Global::release(data); }
};

}  // namespace Perimortem::Memory::Global
