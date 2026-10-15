// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/access/vector.hpp"
#include "perimortem/core/data.hpp"
#include "perimortem/core/view/bytes.h"

namespace Perimortem::Memory::Allocator {

// Allocates objects whose access ends together when the Arena resets or is
// destroyed, allowing fast allocation and bulk storage reclamation.
//
// An arena avoids the bookkeeping overhead of an allocation header for every
// object. This makes it useful when many small objects must be created quickly
// and discarded together, such as a deserialized JSON document or one compiler
// transaction.
//
// `construct()` begins a C++ object lifetime in Arena storage. `Arena::reset()`
// ends every access promise without calling individual destructors. A later
// allocation can reuse retained storage and thereby end the former C++ object
// lifetime. Arena values must therefore release no independently owned
// resources from their destructors. In particular, rented Bibliotheca storage
// must be remitted before the Arena is reset or destroyed.
//
// Arena has a stable identity. Objects and Managed containers retain its
// address, so copying or moving an Arena would separate those borrowers from
// the storage lifetime they joined.
class Arena {
 public:
  // Ordinary requests share 32 KiB pages. Each page includes the link used to
  // return earlier pages. A larger request receives a dedicated larger page.
  static constexpr U64 page_size = (1 << 15);
  static constexpr U64 arena_alignment = sizeof(Count);

  EXPORTED(PERIMORTEM) Arena();
  EXPORTED(PERIMORTEM) ~Arena();
  Arena(const Arena&) = delete;
  Arena(Arena&&) = delete;
  auto operator=(const Arena&) -> Arena& = delete;
  auto operator=(Arena&&) -> Arena& = delete;

  inline auto allocate(Count bytes_requested) -> Core::Access::Bytes {
    // Fetch a new page if we are full due to either running out of our current
    // page, or needing to allocate an object larger than our page size.
    //
    // An arena favors cheap allocation over page demotion. A long lived arena
    // can therefore retain an unusually large page after one large request.
    // Use it for bounded transactions rather than an unbounded object cache.
    if (usage > page_size || bytes_requested > page_size - usage) {
      fetch_page(bytes_requested);
    }

    // Round the next cursor to `arena_alignment`. Every returned block can then
    // hold any type admitted by `reserve`.
    U8* root = rented_block + usage;
    usage = Core::Data::align<arena_alignment>(usage + bytes_requested);
    return Core::Access::Bytes(root, bytes_requested);
  }

  // Reserves storage for one object without beginning its lifetime.
  template <typename type>
  auto reserve() -> type& {
    static_assert(alignof(type) <= arena_alignment);
    return *Core::Data::cast<type>(allocate(sizeof(type)).get_data());
  }

  // Reserves aligned storage for `size` values without beginning their
  // lifetimes. The caller constructs every value before access.
  template <typename type>
  auto reserve(Count size) -> Core::Access::Vector<type> {
    static_assert(alignof(type) <= arena_alignment);
    // Keep an overflowing element count on the allocation failure path rather
    // than returning a small buffer with a large advertised range.
    const Count bytes =
        size > Count(-1) / sizeof(type) ? Count(-1) : sizeof(type) * size;
    auto ptr = allocate(bytes).get_data();
    return Core::Access::Vector<type>(Core::Data::cast<type>(ptr), size);
  }

  // Allocates and constructs one object whose lifetime is owned by the arena.
  template <typename type, typename... arg_types>
  auto construct(arg_types&&... args) -> type& {
    static_assert(alignof(type) <= arena_alignment);
    U8* ptr = allocate(sizeof(type)).get_data();
    return Toolchain::Memory::Lifetime::construct<type>(
        ptr, [&args...] { return type(static_cast<arg_types&&>(args)...); });
  }

  // Allocates one object from an exact value produced by its owning factory.
  //
  // Guaranteed copy elision lets the factory keep constructor access while the
  // object begins its lifetime directly in arena storage. This supports
  // immovable objects without making their constructors public.
  template <typename type, typename factory_type>
  auto construct_from(factory_type&& factory) -> type& {
    static_assert(alignof(type) <= arena_alignment);
    static_assert(
        __is_same(type, decltype(static_cast<factory_type&&>(factory)())));
    U8* ptr = allocate(sizeof(type)).get_data();
    return Toolchain::Memory::Lifetime::construct<type>(
        ptr, static_cast<factory_type&&>(factory));
  }

  // Copies every source value into storage owned by this Arena. The returned
  // View remains accessible until this Arena resets or is destroyed.
  auto copy(Core::View::Bytes source) -> Core::View::Bytes {
    if (source.is_empty()) {
      return Perimortem::Core::View::Bytes();
    }

    auto access = allocate(source.get_size());
    Core::Data::copy(access.get_data(), source.get_data(), source.get_size());
    return access;
  }

  // Invalidates every pointer and reference obtained from this Arena. No object
  // destructor runs. The retained page may be reused by the next allocation,
  // so former values cannot be inspected after return.
  EXPORTED(PERIMORTEM) auto reset() -> void;

 private:
  // `allocate` keeps the bump path inline. Page acquisition remains out of line
  // so consumers share one implementation of Bibliotheca page ownership.
  EXPORTED(PERIMORTEM) auto fetch_page(Count bytes_requested) -> void;

  U8* rented_block;
  Count usage;
};

}  // namespace Perimortem::Memory::Allocator
