// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/memory/lifetime.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/memory/allocation.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/container/set.hpp"

namespace Perimortem::Memory::Managed {

// Managed Set stores control hashes and keys under one Arena lifetime.
// Rehashing constructs a current table while former keys remain at their
// previous addresses until the Arena resets. A pointer to a former key
// continues to designate the key left by that copy or move and does not follow
// later Set changes.
//
// Arena reset reclaims the table allocations together. Clearing, removal and
// destruction therefore invoke no key destructors. Key types cannot depend on
// individual destructors to release separate allocations, handles or
// Bibliotheca blocks. Every address retained by a key remains valid only under
// the same Arena.
//
// The current table is allocated from the recorded Arena. Copy construction
// creates an independent table in the source Arena. Assignment retains the
// destination Arena and copies the current keys into its storage. Move
// construction adopts the source Arena and table, leaving the source empty.
template <typename key_type>
class Set final : public Container::Set<Set<key_type>, key_type> {
  using Base = Container::Set<Set<key_type>, key_type>;
  friend Base;

 public:
  Set() = delete;
  // An Arena selects the allocation and key lifetime policy rather than
  // supplying keys. Explicit construction prevents an Arena argument from
  // implicitly creating an empty Set.
  constexpr explicit Set(Allocator::Arena& selected_arena)
      : arena(selected_arena) {}

  Set(Allocator::Arena& selected_arena, Count reserved_capacity)
      : arena(selected_arena) {
    Base::ensure_capacity(reserved_capacity);
  }

  Set(Allocator::Arena& selected_arena, const Set& source)
    requires(__is_constructible(key_type, const key_type&))
      : arena(selected_arena) {
    Base::copy_from(source);
  }

  Set(const Set& rhs)
    requires(__is_constructible(key_type, const key_type&))
      : arena(rhs.arena) {
    Base::copy_from(rhs);
  }

  constexpr Set(Set&& rhs) : arena(rhs.arena) {
    Base::take(Core::Data::take(rhs));
  }

  constexpr ~Set() = default;

  auto operator=(const Set& rhs) -> Set&
    requires(__is_constructible(key_type, const key_type&))
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
    static_assert(alignof(key_type) <= Allocator::Arena::arena_alignment);
    auto allocation = arena.allocate(requested_bytes);
    return Allocation<U8>(allocation.get_data(), allocation.get_size());
  }

  // A Managed key stored in this Set must use the Set's Arena so reset of
  // another Arena cannot invalidate data reachable through the key. A key
  // already using this Arena can move into the table. A key from another Arena
  // is copied into this Arena. Keys without an Arena contract use ordinary
  // construction.
  template <typename type>
  static constexpr Bool has_arena_copy =
      __is_constructible(type, Allocator::Arena&, const type&) &&
      requires(const type& selected) { selected.get_arena(); };

  template <typename type, typename factory_type>
  constexpr auto construct(type* destination, factory_type factory) -> type& {
    if constexpr (has_arena_copy<type>) {
      type candidate = factory();
      return Toolchain::Memory::Lifetime::construct<type>(
          destination, [this, &candidate] {
            if (&candidate.get_arena() == &arena) {
              return type(Core::Data::take(candidate));
            }

            return type(arena, candidate);
          });
    }

    return Toolchain::Memory::Lifetime::construct<type>(
        destination, Core::Data::take(factory));
  }

  // Rehashing leaves the former table accessible until Arena reset. Copying
  // constructs the current key in uninitialized storage at `destination`
  // without changing `source`. A move only key cannot be copied into a second
  // current key, so relocation move constructs `destination` and leaves
  // `source` alive in its moved from state.
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

  // Removal excludes a key from the current table without reclaiming its Arena
  // storage. Arena reset ends access to every key and table allocation
  // together.
  template <typename type>
  static constexpr auto retire(type*, Count) -> void {}

  // Arena reset reclaims every table allocation, so detaching a table requires
  // no individual release operation.
  static constexpr auto release(U8*) -> void {}

  Allocator::Arena& arena;
};

}  // namespace Perimortem::Memory::Managed
