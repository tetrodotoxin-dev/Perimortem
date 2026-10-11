// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/scalar.hpp"
#include "perimortem/core/view/vector.hpp"
#include "perimortem/memory/allocator/arena.hpp"

namespace Perimortem::Memory::Managed {

// A Managed::Vector stores a pointer to it's allocator rather than using the
// default Bibliotheca allocator. It assumes that all objects it manages also
// come out of the same allocator and that they do not need to be destructed.
//
// This provides a useful typed wrapper around Arena's slab allocation which is
// a common temporary compute pattern used to optimize away allocator overhead.
//
// Any pointers to values in the Vector are guranteed stable, even if the Vector
// grows, as long as the Arena used by this Vector isn't cleared.
template <typename value_type>
class Vector {
 public:
  static constexpr Count start_capacity = 8;
  static constexpr Count growth_factor = 2;

  Vector(const Vector&) = default;
  constexpr Vector(Allocator::Arena& arena) : arena(arena) { reset(); }

  constexpr operator Core::View::Vector<value_type>() const {
    return Core::View::Vector<value_type>(rented_block, size);
  }

  constexpr auto clear() -> void { size = 0; }

  constexpr auto reset() -> void {
    size = 0;
    capacity = start_capacity;
    rented_block = Core::Data::cast<value_type>(
        arena.allocate(sizeof(value_type) * start_capacity).get_data());
  }

  constexpr auto reset(Count reserve_capacity) -> void {
    if (reserve_capacity <= start_capacity) {
      reserve_capacity = start_capacity;
    }

    size = 0;
    capacity = reserve_capacity;
    rented_block = Core::Data::cast<value_type>(
        arena.allocate(sizeof(value_type) * reserve_capacity).get_data());
  }

  constexpr auto insert(const value_type& data) -> void {
    ensure_capacity(size + 1);

    // Construct using the copy constructor.
    Toolchain::Memory::Lifetime::construct<value_type>(
        rented_block + (size++), [&data] { return value_type(data); });
  }

  constexpr auto prepend(const value_type& data) -> void {
    ensure_capacity(size + 1);
    if (size != 0) {
      memmove(rented_block + 1, rented_block, sizeof(value_type) * size);
    }

    Toolchain::Memory::Lifetime::construct<value_type>(
        rented_block, [&data] { return value_type(data); });
    size++;
  }

  constexpr auto emplace(value_type&& data) -> value_type& {
    ensure_capacity(size + 1);

    // Construct using the move constructor.
    return Toolchain::Memory::Lifetime::construct<value_type>(
        rented_block + (size++),
        [&data] { return value_type(Core::Data::take(data)); });
  }

  constexpr auto contains(const value_type& data) const -> Bool {
    return get_view().contains(data);
  }

  constexpr auto at(Count index) -> value_type& { return rented_block[index]; }

  constexpr auto at(Count index) const -> const value_type& {
    return rented_block[index];
  }

  constexpr auto operator[](Count index) -> value_type& { return at(index); }
  constexpr auto operator[](Count index) const -> const value_type& {
    return at(index);
  }

  constexpr auto get_size() const -> Count { return size; }
  constexpr auto is_empty() const -> Bool { return size == 0; }
  constexpr auto get_capacity() const -> Count { return capacity; };
  constexpr auto get_arena() const -> Allocator::Arena& { return arena; }
  constexpr auto get_view() const -> Core::View::Vector<value_type> {
    return Core::View::Vector<value_type>(rented_block, size);
  }

 private:
  // Ensures there is _at least_ enough room for the requested number of
  // objects.
  auto ensure_capacity(Count required_size) -> void {
    // Check if we can already fit required buffer.
    if (required_size <= get_capacity()) {
      return;
    }

    // Attempt to grow by a factor of 2.
    // If that doesn't work than grow to exact size.
    const auto new_capacity =
        Core::Scalar::max(get_capacity() * 2, required_size);

    // Fetch and transfer to new block.
    auto new_block = arena.reserve<value_type>(new_capacity).get_data();

    // Copy the raw bytes of the block
    if (rented_block) {
      memcpy((void*)new_block, rented_block, sizeof(value_type) * size);
    }

    // Update block and get the new capacity.
    rented_block = new_block;
    capacity = new_capacity;
  }

  Allocator::Arena& arena;
  value_type* rented_block;
  Count size;
  Count capacity;
};

}  // namespace Perimortem::Memory::Managed
