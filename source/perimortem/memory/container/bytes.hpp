// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/access/bytes.hpp"
#include "perimortem/core/algorithm/hash.hpp"
#include "perimortem/core/data.hpp"
#include "perimortem/core/storage/option.hpp"
#include "perimortem/core/view/bytes.hpp"

namespace Perimortem::Memory::Container {

// Bytes represents contiguous mutable U8 values with independent size and
// allocation capacity. It separates range and overlap behavior from
// `policy_type` allocation, growth and release so storage policies can change
// allocation and lifetime rules without reimplementing value operations.
//
// The empty representation is guaranteed to be allocation free. A wrapper that
// exposes default construction produces it in constexpr O(1) time. `clear()`
// retains storage for reuse while `reset()` detaches the current allocation.
// The policy decides when detached storage can be reclaimed.
//
// `get_view()` always observes the current values without changing capacity.
// `get_access()` forces capacity to be nonzero while `try_access()` leaves
// capacity unchanged and returns None when it is zero. Neither operation
// changes size or adds a current value.
template <typename policy_type>
class Bytes {
 public:
  auto operator==(const Bytes& rhs) const -> Bool {
    return get_view() == rhs.get_view();
  }

  auto operator==(const Core::View::Bytes& rhs) const -> Bool {
    return get_view() == rhs;
  }

  constexpr operator Core::View::Bytes() const { return get_view(); }

  EXPORTED(PERIMORTEM) auto append(U8 byte) -> void;
  EXPORTED(PERIMORTEM) auto append(U8 byte, Count amount) -> void;

  // Appends the values in `view` to this Bytes object. The source may refer to
  // the current allocation because growth finishes copying it before releasing
  // that allocation.
  EXPORTED(PERIMORTEM) auto concat(Core::View::Bytes view) -> void;

  // Replaces the current values with a copy. The source may overlap the current
  // allocation because replacement keeps those values valid until copying ends.
  EXPORTED(PERIMORTEM) auto copy(Core::View::Bytes view) -> void;

  // Changes size while preserving values that remain within the new range.
  // Growth within the current capacity does not allocate. Every added value is
  // initialized to zero. Shrinking, including to zero, neither allocates nor
  // releases storage so repeated stack operations can reuse the capacity.
  EXPORTED(PERIMORTEM) auto resize(Count new_size) -> void;

  // Changes size without preserving current values. The allocation is reused
  // only when the requested size is greater than half capacity and does not
  // exceed capacity. Every other nonzero size resets the Bytes object before
  // asking its policy for replacement capacity, avoiding a copy and allowing
  // the policy to select storage for the new size.
  //
  // A requested size of zero sets the Bytes object to its empty representation.
  // Repeating that request does not change its state. Reclaiming the underlying
  // storage remains a policy decision. New values have unspecified state and
  // must be completely overwritten before they are read.
  EXPORTED(PERIMORTEM) auto forgetful_resize(Count new_size) -> void;

  // Destructively slices and extends the stored values. A positive `start`
  // removes values from the front, while a negative `start` shifts them right.
  // Either shift moves retained values at O(n) cost. Positions introduced by
  // shifting or extension are initialized to zero.
  //
  // A reframe is allocation free unless the newly requested size is larger than
  // the current capacity.
  EXPORTED(PERIMORTEM) auto reframe(S64 start, Count new_size) -> void;

  // Returns the byte at `index`, or None when the index is outside the current
  // size. The returned reference remains current until an operation changes
  // the size or replaces the allocation.
  EXPORTED(PERIMORTEM) auto operator[](Count index)
      -> Core::Storage::Option<U8&>;
  EXPORTED(PERIMORTEM) auto operator[](Count index) const
      -> Core::Storage::Option<const U8&>;
  // Writes `value` to every value within the current size. The size bounds this
  // safe `memset` operation, so no separate byte count is accepted.
  EXPORTED(PERIMORTEM) auto set(U8 value) -> void;
  EXPORTED(PERIMORTEM) auto slice(Count start, Count size) const
      -> Core::View::Bytes;

  constexpr auto get_size() const -> Count { return size; }
  constexpr auto get_capacity() const -> Count { return capacity; }
  // Exposes the allocation start for unchecked pointer access. The caller
  // keeps every index below the current size and reacquires the pointer after
  // an operation that can replace or reset storage.
  constexpr auto get_data() -> U8* { return source_block; }
  constexpr auto get_data() const -> const U8* { return source_block; }
  constexpr auto get_view() const -> Core::View::Bytes {
    if (is_empty()) {
      return Core::View::Bytes();
    }

    return Core::View::Bytes(source_block[0], size);
  }

  // Getting access forces capacity to be nonzero so the returned Access always
  // has a valid storage address. When the container has zero capacity it
  // allocates enough capacity for at least one value.
  //
  // Calling `ensure_capacity` before `get_access` establishes a custom capacity
  // floor and prevents `get_access` from allocating again.
  auto get_access() -> Core::Access::Bytes {
    if (source_block == nullptr) {
      ensure_capacity(1);
    }

    return Core::Access::Bytes(source_block[0], size);
  }

  // Returns writable access without changing capacity. None means capacity is
  // zero. A selected empty Access means capacity remains available while size
  // is zero, such as after `clear()`.
  constexpr auto try_access() -> Core::Storage::Option<Core::Access::Bytes> {
    return Core::Access::Bytes::create({source_block, size});
  }

  auto hash() const -> U64 {
    return Core::Algorithm::Hash(get_view()).get_value();
  }

  constexpr auto is_empty() const -> Bool { return size == 0; }

  EXPORTED(PERIMORTEM) auto clear() -> void;
  EXPORTED(PERIMORTEM) auto reset() -> void;
  EXPORTED(PERIMORTEM) auto ensure_capacity(Count required_size) -> void;

 protected:
  constexpr Bytes() = default;

  // Allocation reaches the derived policy only after wrapper construction has
  // established policy state. Release remains static because base destruction
  // follows the end of that state.
  constexpr ~Bytes() {
    if (source_block) {
      policy_type::release(source_block);
    }
  }

  constexpr auto take(Bytes&& rhs) -> void {
    source_block = rhs.source_block;
    size = rhs.size;
    capacity = rhs.capacity;
    rhs.source_block = nullptr;
    rhs.size = 0;
    rhs.capacity = 0;
  }

 private:
  constexpr auto get_policy() -> policy_type& {
    return *Core::Data::cast<policy_type>(this);
  }

  U8* source_block = nullptr;
  Count size = 0;
  Count capacity = 0;
};

}  // namespace Perimortem::Memory::Container
