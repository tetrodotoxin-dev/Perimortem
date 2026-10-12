// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.hpp"

#include "perimortem/core/access/vector.hpp"
#include "perimortem/core/data.hpp"
#include "perimortem/core/view/vector.hpp"
#include "perimortem/memory/allocation.hpp"

namespace Perimortem::Memory::Container {

// Vector separates contiguous sequence operations from allocation and element
// lifetime policy. Policy wrappers can therefore impose ownership, reclamation,
// and growth invariants without duplicating the shared sequence logic.
//
// The policy owns allocation, capacity growth, element construction,
// replacement, retirement and release. Another wrapper can define different
// storage and lifetime invariants without changing this sequence capability.
//
// Copies receive an independent current block. Moves transfer the current block
// and leave their source empty and reusable under its original policy.
//
// Both the policy and element type are open template arguments. The
// implementation remains in this header because those open arguments prevent
// the explicit instantiation boundary used by `Container::Bytes`.
template <typename policy_type, typename type>
class Vector {
 public:
  constexpr operator Core::View::Vector<type>() const { return get_view(); }
  constexpr operator Core::Access::Vector<type>() { return get_access(); }

  constexpr auto insert(const type& data) -> type& {
    if (!ensure_capacity(size + 1, data)) {
      construct_at(source_block + size, [&data] { return type(data); });
    }

    return source_block[size++];
  }

  constexpr auto emplace(type&& data) -> type& {
    if (!ensure_capacity(size + 1, static_cast<type&&>(data))) {
      construct_at(source_block + size, [&data] {
        return type(static_cast<type&&>(data));
      });
    }

    return source_block[size++];
  }

  // Removes the selected element in O(1) element operations by sacrificing
  // relative order. The last element replaces it before the logical size
  // decreases by one. Pointers and references to every other element except
  // the former last element remain valid. An index outside the current size
  // leaves the vector unchanged and returns False.
  constexpr auto remove(Count index) -> Bool {
    if (index >= size) {
      return False;
    }

    const Count last_index = size - 1;
    if constexpr (__is_trivially_copyable(type)) {
      if (index != last_index) {
        if consteval {
          replace_relocated(source_block + index, source_block[last_index]);
        } else {
          memcpy(source_block + index, source_block + last_index, sizeof(type));
        }
      }
    } else {
      if (index != last_index) {
        replace_relocated(source_block + index, source_block[last_index]);
      }
    }

    get_policy().retire(source_block + last_index, 1);
    size--;
    return True;
  }

  // Removes the selected element in O(n) element operations to preserve
  // relative order. Each later element moves one position toward the front
  // before the logical size decreases by one. Pointers and references at or
  // after the selected index become invalid. Earlier pointers and references
  // remain valid. An index outside the current size leaves the vector unchanged
  // and returns False.
  constexpr auto remove_stable(Count index) -> Bool {
    if (index >= size) {
      return False;
    }

    const Count last_index = size - 1;
    if constexpr (__is_trivially_copyable(type)) {
      if (index != last_index) {
        if consteval {
          for (Count shift = index; shift < last_index; shift++) {
            replace_relocated(source_block + shift, source_block[shift + 1]);
          }
        } else {
          memmove(
              source_block + index, source_block + index + 1,
              sizeof(type) * (last_index - index));
        }
      }
    } else {
      for (Count shift = index; shift < last_index; shift++) {
        replace_relocated(source_block + shift, source_block[shift + 1]);
      }
    }

    get_policy().retire(source_block + last_index, 1);
    size--;
    return True;
  }

  // Changes the live element count while preserving retained values. The
  // element type must support default construction because a larger size can
  // expose new values. Use `retain` when only shrinking is required.
  constexpr auto resize(Count new_size) -> void
    requires(__is_constructible(type))
  {
    if (new_size == size) {
      return;
    }

    if (new_size < size) {
      retain(0, new_size);
      return;
    }

    ensure_capacity(new_size);
    if constexpr (!__is_trivially_constructible(type)) {
      for (Count index = size; index < new_size; index++) {
        construct_at(source_block + index, [] { return type(); });
      }
    }

    size = new_size;
  }

  // Keeps a clipped subrange without allocating or requiring a default value.
  // The retained element at `start` becomes index zero. A nonzero start
  // relocates every retained value, so existing pointers and references no
  // longer designate vector elements. A zero start preserves access to the
  // retained prefix and retires only its former tail.
  constexpr auto retain(Count start, Count count) -> void {
    if (start >= size || count == 0) {
      clear();
      return;
    }

    const Count available = size - start;
    const Count retained = count < available ? count : available;
    if (start != 0) {
      if constexpr (__is_trivially_copyable(type)) {
        if consteval {
          for (Count index = 0; index < retained; index++) {
            replace_relocated(
                source_block + index, source_block[start + index]);
          }
        } else {
          memmove(source_block, source_block + start, sizeof(type) * retained);
        }
      } else {
        for (Count index = 0; index < retained; index++) {
          replace_relocated(source_block + index, source_block[start + index]);
        }
      }
    }

    retire_range(retained, size);
    size = retained;
  }

  constexpr auto clear() -> void {
    retire_range(0, size);
    size = 0;
  }

  constexpr auto contains(const type& data) const -> Bool {
    return get_view().contains(data);
  }

  constexpr auto at(Count index) const -> const type& {
    return source_block[index];
  }

  constexpr auto at(Count index) -> type& { return source_block[index]; }
  constexpr auto operator[](Count index) const -> const type& {
    return at(index);
  }

  constexpr auto operator[](Count index) -> type& { return at(index); }

  constexpr auto get_size() const -> Count { return size; }
  constexpr auto get_capacity() const -> Count { return capacity; }
  constexpr auto get_view() const -> Core::View::Vector<type> {
    return Core::View::Vector<type>(source_block, size);
  }

  constexpr auto get_data() const -> const type* { return source_block; }
  constexpr auto get_data() -> type* { return source_block; }
  constexpr auto get_access() -> Core::Access::Vector<type> {
    return Core::Access::Vector<type>(source_block, size);
  }

  constexpr auto is_empty() const -> Bool { return size == 0; }

 protected:
  constexpr Vector() = default;
  Vector(const Vector&) = delete;
  Vector(Vector&&) = delete;
  auto operator=(const Vector&) -> Vector& = delete;
  auto operator=(Vector&&) -> Vector& = delete;

  // Policies with independent reclamation release storage from their derived
  // destructor while policy state is still alive. An owning policy member
  // releases through its own destructor. Base destruction follows both and
  // cannot dispatch to them.
  constexpr ~Vector() = default;

  constexpr auto copy_from(const Vector& rhs) -> void {
    ensure_capacity(rhs.size);
    for (Count index = 0; index < rhs.size; index++) {
      construct_at(source_block + index, [&rhs, index] {
        return type(rhs.source_block[index]);
      });
    }

    size = rhs.size;
  }

  constexpr auto take(Vector&& rhs) -> void {
    source_block = rhs.source_block;
    size = rhs.size;
    capacity = rhs.capacity;
    rhs.source_block = nullptr;
    rhs.size = 0;
    rhs.capacity = 0;
  }

  constexpr auto reset_storage() -> void { release_storage(); }

  constexpr auto reset_storage(Count requested_capacity) -> void {
    release_storage();
    if (requested_capacity == 0) {
      return;
    }

    auto allocation = get_policy().allocate(requested_capacity);
    source_block = allocation.get_data();
    capacity = allocation.get_capacity();
    get_policy().adopt(static_cast<decltype(allocation)&&>(allocation));
  }

  // Publishes a logical extent without invoking element lifetime operations.
  // A policy exposes this primitive only when trivial values and its storage
  // strategy make destructive resizing part of that policy's public contract.
  constexpr auto set_forgetful_size(Count new_size) -> void
    requires(
        __is_trivially_constructible(type) && __is_trivially_destructible(type))
  {
    size = new_size;
  }

  constexpr auto prepend_copy(const type& data) -> type& {
    if (size == 0) {
      return insert(data);
    }

    // Moving a nonempty range in place would overlap live objects and require
    // assignment. A new block supports nonassignable values and keeps every
    // source alive until its replacement has been constructed.
    const Count required = size + 1;
    const Count requested =
        required <= capacity ? capacity
                             : get_policy().next_capacity(capacity, required);
    auto allocation = get_policy().allocate(requested);

    construct_at(allocation.get_data(), [&data] { return type(data); });
    relocate_values(allocation.get_data() + 1, source_block, size);
    retire_replaced_storage();

    source_block = allocation.get_data();
    capacity = allocation.get_capacity();
    size = required;
    get_policy().adopt(static_cast<decltype(allocation)&&>(allocation));
    return source_block[0];
  }

  // Preserve impossible counts until the allocator reports failure. Wrapping
  // this product would turn an oversized request into a small allocation.
  static constexpr auto allocation_size(Count count) -> Count {
    return count > Count(-1) / sizeof(type) ? Count(-1) : count * sizeof(type);
  }

 private:
  constexpr auto get_policy() -> policy_type& {
    return static_cast<policy_type&>(*this);
  }

  template <typename factory_type>
  constexpr auto construct_at(type* destination, factory_type&& factory)
      -> type& {
    return get_policy().construct(
        destination, static_cast<factory_type&&>(factory));
  }

  constexpr auto construct_relocated(type* destination, type& source) -> void {
    get_policy().relocate(destination, source);
  }

  constexpr auto replace_relocated(type* destination, type& source) -> void {
    get_policy().replace(destination, source);
  }

  constexpr auto retire_range(Count begin, Count end) -> void {
    if (begin == end) {
      return;
    }

    get_policy().retire(source_block + begin, end - begin);
  }

  constexpr auto relocate_values(type* destination, type* source, Count count)
      -> void {
    if (source == nullptr) {
      return;
    }

    // Byte copying preserves trivial values across both raw and constructed
    // storage. Nontrivial values delegate lifetime creation to their policy.
    if constexpr (__is_trivially_copyable(type)) {
      if consteval {
        for (Count index = 0; index < count; index++) {
          construct_relocated(destination + index, source[index]);
        }
      } else {
        memcpy(destination, source, sizeof(type) * count);
      }
    } else {
      for (Count index = 0; index < count; index++) {
        construct_relocated(destination + index, source[index]);
      }
    }
  }

  constexpr auto retire_replaced_storage() -> void {
    if (source_block) {
      get_policy().release(source_block, size);
    }
  }

  constexpr auto release_storage() -> void {
    retire_replaced_storage();
    source_block = nullptr;
    size = 0;
    capacity = 0;
  }

  template <typename... argument_types>
  constexpr auto ensure_capacity(
      Count required_size,
      argument_types&&... arguments) -> Bool {
    if (required_size <= capacity) {
      return False;
    }

    const Count requested = get_policy().next_capacity(capacity, required_size);
    auto allocation = get_policy().allocate(requested);
    if constexpr (sizeof...(arguments) != 0) {
      // An inserted argument may refer to a current element. Constructing the
      // pending value first keeps its source alive until it reaches the new
      // block, before relocation can destroy or move from that source.
      construct_at(allocation.get_data() + size, [&arguments...] {
        return type(static_cast<argument_types&&>(arguments)...);
      });
    }

    relocate_values(allocation.get_data(), source_block, size);
    retire_replaced_storage();
    source_block = allocation.get_data();
    capacity = allocation.get_capacity();
    get_policy().adopt(static_cast<decltype(allocation)&&>(allocation));
    return True;
  }

  type* source_block = nullptr;
  Count size = 0;
  Count capacity = 0;
};

}  // namespace Perimortem::Memory::Container
