// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/memory/lifetime.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/memory/allocation.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/container/map.hpp"

namespace Perimortem::Memory::Managed {

// Managed Map stores control hashes, keys and values under one Arena lifetime.
// Rehashing constructs a current table while former keys and values remain at
// their previous addresses until the Arena resets. A pointer to a former key or
// value continues to designate the key or value left by that copy or move and
// does not follow later Map writes.
//
// Arena reset reclaims the table allocations together. Clearing, removal,
// replacement and destruction therefore invoke no key or value destructors.
// Key and value types cannot depend on individual destructors to release
// separate allocations, handles or Bibliotheca blocks. Every address they
// retain remains valid only under the same Arena.
//
// The current table is allocated from the recorded Arena. Copy construction
// creates an independent table in the source Arena. Assignment retains the
// destination Arena and copies the current key value pairs into its storage.
// Move construction adopts the source Arena and table, leaving the source
// empty.
template <typename key_type, typename value_type>
class Map final
    : public Container::Map<Map<key_type, value_type>, key_type, value_type> {
  using Base = Container::Map<Map<key_type, value_type>, key_type, value_type>;
  friend Base;

 public:
  Map() = delete;
  // An Arena selects the allocation and key and value lifetime policy rather
  // than supplying key value pairs. Explicit construction prevents an Arena
  // argument from implicitly creating an empty Map.
  constexpr explicit Map(Allocator::Arena& selected_arena)
      : arena(selected_arena) {}

  Map(Allocator::Arena& selected_arena, Count reserved_capacity)
      : arena(selected_arena) {
    Base::ensure_capacity(reserved_capacity);
  }

  Map(Allocator::Arena& selected_arena, const Map& source)
    requires(
        __is_constructible(key_type, const key_type&) &&
        __is_constructible(value_type, const value_type&))
      : arena(selected_arena) {
    Base::copy_from(source);
  }

  Map(const Map& rhs)
    requires(
        __is_constructible(key_type, const key_type&) &&
        __is_constructible(value_type, const value_type&))
      : arena(rhs.arena) {
    Base::copy_from(rhs);
  }

  constexpr Map(Map&& rhs) : arena(rhs.arena) {
    Base::take(Core::Data::take(rhs));
  }

  constexpr ~Map() = default;

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

  constexpr auto get_arena() const -> Allocator::Arena& { return arena; }

 private:
  auto allocate(Count requested_bytes) -> Allocation<U8> {
    static_assert(
        alignof(typename Base::Bucket) <= Allocator::Arena::arena_alignment);
    auto allocation = arena.allocate(requested_bytes);
    return Allocation<U8>(allocation.get_data(), allocation.get_size());
  }

  // A Managed key or value stored in this Map must use the Map's Arena so reset
  // of another Arena cannot invalidate reachable data. A key or value already
  // using this Arena can move into the table. One from another Arena is copied
  // into this Arena. Types without an Arena contract use ordinary construction.
  template <typename type>
  static constexpr Bool has_arena_copy =
      __is_constructible(type, Allocator::Arena&, const type&) &&
      requires(const type& selected) { selected.get_arena(); };

  template <typename type>
  constexpr auto materialize(type& selected) -> type {
    if constexpr (has_arena_copy<type>) {
      if (&selected.get_arena() != &arena) {
        return type(arena, selected);
      }
    }

    if constexpr (__is_constructible(type, type&&)) {
      return type(Core::Data::take(selected));
    } else {
      return type(selected);
    }
  }

  template <typename type, typename factory_type>
  constexpr auto construct(type* destination, factory_type factory) -> type& {
    if constexpr (
        __is_same(type, typename Base::Bucket) &&
        (has_arena_copy<key_type> || has_arena_copy<value_type>)) {
      type candidate = factory();
      return Toolchain::Memory::Lifetime::construct<type>(
          destination, [this, &candidate] {
            return type(
                materialize(candidate.key), materialize(candidate.value));
          });
    } else if constexpr (has_arena_copy<type>) {
      type candidate = factory();
      return Toolchain::Memory::Lifetime::construct<type>(
          destination, [this, &candidate] { return materialize(candidate); });
    }

    return Toolchain::Memory::Lifetime::construct<type>(
        destination, Core::Data::take(factory));
  }

  // Rehashing leaves the former table accessible until Arena reset. Copying
  // constructs the current key value pair in uninitialized storage at
  // `destination` without changing `source`. A move only key value pair cannot
  // be copied into a second current pair, so relocation move constructs
  // `destination` and leaves `source` alive in its moved from state.
  template <typename type>
  constexpr auto relocate(type* destination, type& source) -> void {
    if constexpr (__is_constructible(type, const type&)) {
      const type& copy_source = source;
      construct(destination, [&copy_source] { return type(copy_source); });
    } else {
      construct(
          destination, [&source] { return type(Core::Data::take(source)); });
    }
  }

  // Removal excludes a key value pair from the current table without
  // reclaiming its Arena storage. Arena reset ends access to every key value
  // pair and table allocation together.
  template <typename type>
  static constexpr auto retire(type*, Count) -> void {}

  // Assignment materializes the replacement under the Arena before reusing
  // `destination`. This keeps sources derived from the current value accessible
  // while preserving the policy that no Managed value is destructed
  // independently. The intermediate value remains in Arena storage until reset.
  template <typename type, typename factory_type>
  constexpr auto replace(type* destination, factory_type factory) -> type& {
    type& candidate = arena.construct_from<type>(Core::Data::take(factory));
    return construct(destination, [&candidate] {
      if constexpr (__is_constructible(type, type&&)) {
        return type(Core::Data::take(candidate));
      } else {
        return type(candidate);
      }
    });
  }

  // Arena reset reclaims every table allocation, so detaching a table requires
  // no individual release operation.
  static constexpr auto release(U8*) -> void {}

  Allocator::Arena& arena;
};

}  // namespace Perimortem::Memory::Managed
