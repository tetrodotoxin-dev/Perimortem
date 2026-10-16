// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.hpp"

#include "perimortem/core/access/vector.hpp"
#include "perimortem/core/data.hpp"
#include "perimortem/core/option.hpp"
#include "perimortem/core/view/vector.hpp"
#include "perimortem/memory/allocation.hpp"

namespace Perimortem::Memory::Container {

// Vector represents contiguous mutable values with independent size and
// allocation capacity. It separates sequence behavior from `policy_type`
// allocation, growth and release so storage policies can change allocation and
// lifetime rules without reimplementing value operations.
//
// The empty representation is guaranteed to be allocation free. A wrapper that
// exposes default construction produces it in constexpr O(1) time. `clear()`
// retains storage for reuse while `reset()` detaches the current allocation.
// The policy decides when detached storage can be reclaimed.
//
// Arbitrary values can require nontrivial construction and retirement. Vector
// routes those lifetime transitions through the policy instead of assuming
// every retired value is immediately destroyed.
//
// Copies receive an independent current allocation. Moves transfer the current
// allocation and leave their source empty and reusable under its original
// policy.
//
// A returned reference identifies the same current value until an operation
// replaces the allocation or relocates or replaces the value at that address.
// Direct value mutation is supported.
//
// Both the policy and value type are open template arguments. The
// implementation remains in this header because those open arguments prevent
// the explicit instantiation boundary used by `Container::Bytes`.
//
// The policy extension contract defines storage and value lifetime transitions
// through the following required operations:
//
//  auto next_capacity(Count current, Count required) -> Count
//
//    Selects an allocation capacity no smaller than `required`. The policy can
//    use `current` to apply its growth strategy.
//
//  auto allocate(Count requested) -> Allocation<type>
//
//    Returns storage with at least the requested value capacity and the
//    alignment required by `type`. Its values can be established through
//    `construct` or `relocate` and remain accessible until `release`.
//
//  auto adopt(Allocation<type>&& allocation) -> void
//
//    Receives `allocation` after Vector publishes its data and capacity. A
//    policy can retain its allocation record here but cannot invalidate either
//    published value.
//
//  auto construct(type* destination, factory_type factory) -> type&
//
//    Establishes one current value at `destination` from exactly one call to
//    `factory`. The destination is available for a new value under the policy.
//
//  auto relocate(type* destination, type& source) -> void
//
//    Establishes a current value in a replacement allocation. The source
//    remains accessible until Vector releases its allocation.
//
//  auto replace(type* destination, type& source) -> void
//
//    Replaces the current value at `destination` from `source` without changing
//    the allocation. The policy performs the required lifetime transition.
//
//  auto retire(type* values, Count count) -> void
//
//    Excludes the current value range from Vector while retaining its
//    containing allocation. The policy applies its value lifetime rule to that
//    range.
//
//  auto release(type* values, Count count) -> void
//
//    Detaches the allocation containing the supplied current values. The policy
//    applies their remaining lifetime rules and controls storage reclamation.
//
// Policies deliberately inject stronger invariants through those operations.
// Thread affinity, process heap rules and Arena lifetime rules remain in their
// policy wrappers while the meanings of Vector operations remain shared.
template <typename policy_type, typename type>
class Vector {
 public:
  constexpr operator Core::View::Vector<type>() const { return get_view(); }
  constexpr operator Core::Access::Vector<type>() { return get_access(); }

  // Appends a copy of `value` and returns its stored value. `value` may refer
  // to a current value because growth constructs the copy before relocating
  // storage.
  constexpr auto insert(const type& value) -> type& {
    return insert_value([&value] { return type(value); });
  }

  // Appends by moving `value` and returns its stored value. `value` may refer
  // to a current value because growth transfers it before relocating storage.
  // That current value remains alive in its moved from state until relocation.
  constexpr auto insert(type&& value) -> type& {
    return insert_value([&value] { return type(Core::Data::take(value)); });
  }

  // Appends copies of `view`. The source may refer to the current Vector
  // because growth constructs the appended values before relocating the
  // current allocation.
  constexpr auto concat(Core::View::Vector<type> view) -> void
    requires(__is_constructible(type, const type&))
  {
    if (view.is_empty()) {
      return;
    }

    const Count previous_size = size;
    const Count required_size = size + view.get_size();
    if (required_size <= capacity) {
      for (Count index = 0; index < view.get_size(); index++) {
        get_policy().construct(
            source_block + previous_size + index,
            [&view, index] { return type(view.get_data()[index]); });
      }
    } else {
      const Count requested_capacity =
          get_policy().next_capacity(capacity, required_size);
      auto allocation = get_policy().allocate(requested_capacity);
      for (Count index = 0; index < view.get_size(); index++) {
        get_policy().construct(
            allocation.get_data() + previous_size + index,
            [&view, index] { return type(view.get_data()[index]); });
      }

      replace_allocation(Core::Data::take(allocation));
    }

    size = required_size;
  }

  // Removes the selected value in O(1) value operations by sacrificing relative
  // order. The last value replaces it before the size decreases by one.
  // Every other current value keeps its address and identity. References to the
  // selected value or former last value no longer identify the same current
  // value. An index outside the current size leaves the Vector unchanged and
  // returns False.
  constexpr auto remove(Count index) -> Bool {
    if (index >= size) {
      return False;
    }

    const Count last_index = size - 1;
    if constexpr (__is_trivially_copyable(type)) {
      if (index != last_index) {
        if consteval {
          get_policy().replace(source_block + index, source_block[last_index]);
        } else {
          memcpy(source_block + index, source_block + last_index, sizeof(type));
        }
      }
    } else {
      if (index != last_index) {
        get_policy().replace(source_block + index, source_block[last_index]);
      }
    }

    get_policy().retire(source_block + last_index, 1);
    size--;
    return True;
  }

  // Removes the selected value in O(n) value operations to preserve relative
  // order. Each later value moves one position toward the front before the size
  // decreases by one. Values before the selected index keep their
  // address and identity. References at or after that index no longer identify
  // the same current value. An index outside the current size leaves the Vector
  // unchanged and returns False.
  constexpr auto remove_stable(Count index) -> Bool {
    if (index >= size) {
      return False;
    }

    const Count last_index = size - 1;
    if constexpr (__is_trivially_copyable(type)) {
      if (index != last_index) {
        if consteval {
          for (Count shift = index; shift < last_index; shift++) {
            get_policy().replace(source_block + shift, source_block[shift + 1]);
          }
        } else {
          memmove(
              source_block + index, source_block + index + 1,
              sizeof(type) * (last_index - index));
        }
      }
    } else {
      for (Count shift = index; shift < last_index; shift++) {
        get_policy().replace(source_block + shift, source_block[shift + 1]);
      }
    }

    get_policy().retire(source_block + last_index, 1);
    size--;
    return True;
  }

  // Changes size while preserving values that remain within the new range.
  // Growth within the current capacity does not replace the allocation. Every
  // added value is value initialized. Shrinking, including to zero, retains the
  // allocation while the policy retires each removed value.
  //
  // The value type must support default construction because this operation can
  // grow. `retain()` supports shrinking types without a default constructor.
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
    for (Count index = size; index < new_size; index++) {
      get_policy().construct(source_block + index, [] { return type(); });
    }

    size = new_size;
  }

  // Changes size without preserving current values. The allocation is reused
  // only when `new_size` is greater than half capacity and does not exceed
  // capacity. Every other nonzero size resets the Vector before asking its
  // policy for replacement capacity, avoiding a copy and allowing the policy to
  // select storage for the new size.
  //
  // A requested size of zero sets the Vector to its empty representation. The
  // operation is limited to trivial value lifetimes so policies can discard or
  // expose storage without an observable lifetime transition. Added values
  // have unspecified state and must be completely overwritten before they are
  // read.
  constexpr auto forgetful_resize(Count new_size) -> void
    requires(
        __is_trivially_constructible(type) && __is_trivially_destructible(type))
  {
    if (new_size <= capacity && new_size > (capacity >> 1)) {
      size = new_size;
      return;
    }

    reset();
    ensure_capacity(new_size);
    size = new_size;
  }

  // Destructively slices the stored values without supporting extension.
  // `start` removes values from the front and `count` is clipped to the
  // remaining range. A start outside the current size or a count of zero clears
  // the Vector while retaining its allocation.
  //
  // A nonzero start moves retained values at O(n) cost. No allocation occurs
  // and capacity remains unchanged. The policy retires every omitted value.
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
            get_policy().replace(
                source_block + index, source_block[start + index]);
          }
        } else {
          memmove(source_block, source_block + start, sizeof(type) * retained);
        }
      } else {
        for (Count index = 0; index < retained; index++) {
          get_policy().replace(
              source_block + index, source_block[start + index]);
        }
      }
    }

    retire_range(retained, size);
    size = retained;
  }

  // Removes every current value while retaining capacity for reuse. Each policy
  // applies its declared retirement behavior.
  constexpr auto clear() -> void {
    retire_range(0, size);
    size = 0;
  }

  // Returns the Vector to its allocation free empty representation. The policy
  // handles every current value lifetime and decides when detached storage can
  // be reclaimed.
  constexpr auto reset() -> void {
    if (source_block) {
      get_policy().release(source_block, size);
    }

    source_block = nullptr;
    size = 0;
    capacity = 0;
  }

  // Reserves capacity for at least `required_size` values without changing size
  // or values. Existing references remain current when the request fits the
  // present allocation. Growth relocates every current value.
  constexpr auto ensure_capacity(Count required_size) -> void {
    if (required_size <= capacity) {
      return;
    }

    const Count requested_capacity =
        get_policy().next_capacity(capacity, required_size);
    auto allocation = get_policy().allocate(requested_capacity);
    replace_allocation(Core::Data::take(allocation));
  }

  constexpr auto contains(const type& value) const -> Bool {
    return get_view().contains(value);
  }

  // Returns the value at `index`, or None when the index is outside the current
  // size. The returned reference remains current until an operation replaces
  // the allocation or relocates or replaces the value at that address.
  constexpr auto operator[](Count index) const -> Core::Option<const type&> {
    if (index >= size) {
      return {};
    }

    return source_block[index];
  }

  constexpr auto operator[](Count index) -> Core::Option<type&> {
    if (index >= size) {
      return {};
    }

    return source_block[index];
  }

  constexpr auto get_size() const -> Count { return size; }
  constexpr auto get_capacity() const -> Count { return capacity; }
  constexpr auto get_view() const -> Core::View::Vector<type> {
    return Core::View::Vector<type>(source_block, size);
  }

  // Exposes the allocation start for unchecked pointer access. The caller keeps
  // every index below the current size and reacquires the pointer after an
  // operation that can replace or reset storage. Value removal can also change
  // the value represented at an existing address.
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

  // This base destructor cannot dispatch to policy hooks. A policy that
  // reclaims blocks directly calls `reset()` from its derived destructor while
  // its fields remain available. A policy field that releases storage through
  // its destructor runs before this base automatically.
  constexpr ~Vector() = default;

  constexpr auto copy_from(const Vector& rhs) -> void {
    copy_from(rhs.get_view());
  }

  constexpr auto copy_from(Core::View::Vector<type> rhs) -> void {
    if (rhs.get_size() == 0) {
      reset();
      return;
    }

    ensure_capacity(rhs.get_size());
    for (Count index = 0; index < rhs.get_size(); index++) {
      get_policy().construct(source_block + index, [&rhs, index] {
        return type(rhs.get_data()[index]);
      });
    }

    size = rhs.get_size();
  }

  constexpr auto take(Vector&& rhs) -> void {
    source_block = rhs.source_block;
    size = rhs.size;
    capacity = rhs.capacity;
    rhs.source_block = nullptr;
    rhs.size = 0;
    rhs.capacity = 0;
  }

  // Preserve impossible counts until the allocator reports failure. Wrapping
  // this product would turn an oversized request into a small allocation.
  static constexpr auto allocation_size(Count count) -> Count {
    return count > Count(-1) / sizeof(type) ? Count(-1) : count * sizeof(type);
  }

 private:
  constexpr auto get_policy() -> policy_type& {
    return *Core::Data::cast<policy_type>(this);
  }

  constexpr auto retire_range(Count begin, Count end) -> void {
    if (begin == end) {
      return;
    }

    // The nonempty range guarantees `source_block` designates current storage.
    // Its policy decides whether retirement destroys or only detaches values.
    get_policy().retire(source_block + begin, end - begin);
  }

  template <typename factory_type>
  constexpr auto insert_value(factory_type factory) -> type& {
    const Count index = size;
    const Count required_size = size + 1;
    if (required_size <= capacity) {
      get_policy().construct(source_block + index, Core::Data::take(factory));
    } else {
      const Count requested_capacity =
          get_policy().next_capacity(capacity, required_size);
      auto allocation = get_policy().allocate(requested_capacity);

      // The source used by `factory` may be a current value. Construct the
      // insertion before relocation can move from or retire that source.
      get_policy().construct(
          allocation.get_data() + index, Core::Data::take(factory));
      replace_allocation(Core::Data::take(allocation));
    }

    size++;
    return source_block[index];
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
          get_policy().relocate(destination + index, source[index]);
        }
      } else {
        memcpy(destination, source, sizeof(type) * count);
      }
    } else {
      for (Count index = 0; index < count; index++) {
        get_policy().relocate(destination + index, source[index]);
      }
    }
  }

  template <typename allocation_type>
  constexpr auto replace_allocation(allocation_type&& allocation) -> void {
    // Relocation completes before the policy can reclaim the former allocation.
    // Publish the replacement only after every current value has reached it.
    relocate_values(allocation.get_data(), source_block, size);
    if (source_block) {
      get_policy().release(source_block, size);
    }

    source_block = allocation.get_data();
    capacity = allocation.get_capacity();
    get_policy().adopt(Core::Data::take(allocation));
  }

  type* source_block = nullptr;
  Count size = 0;
  Count capacity = 0;
};

}  // namespace Perimortem::Memory::Container
