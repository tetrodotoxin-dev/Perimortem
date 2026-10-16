// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/data.hpp"
#include "perimortem/core/hash.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/core/storage/option.hpp"

namespace Perimortem::Memory::Container {

// Set stores unique keys for membership testing without exposing an ordering.
// The `policy_type` implementation establishes allocation and key lifetime
// behavior without changing membership or duplicate key semantics.
//
// Set uses open addressing with linear probing. One allocation contains packed
// U32 control hashes followed by an aligned key range. An occupied control sets
// its high bit. Zero terminates a missing key probe, while a sentinel left by
// removal keeps the probe active and can be reused by a later insertion. This
// rejects most hashes without loading key storage. Rehashing at 90% active
// controls keeps a terminating zero available and purges accumulated sentinels.
//
// Each key must maintain the hash and equality characteristics it had when it
// was inserted for its complete lifetime in the Set. Changing either
// characteristic through external state is undefined behavior because lookup
// follows the stored control hash. Set treats every stored key as constant.
// Updating a key requires removing it and inserting the replacement.
//
// `get_capacity()` reports the total number of keys the Set can hold before
// crossing the maximum load. The 90% maximum load belongs to the container
// capability, so every policy derives the same internal bucket count. Calling
// `ensure_capacity(get_capacity())` never reallocates. The high control bit
// records occupancy, leaving 31 bits for a power of two bucket index. A
// capacity requiring more than 2^31 buckets terminates instead of wrapping
// that index. The first nonzero capacity request allocates 16 buckets and later
// growth doubles the bucket count.
//
// The complete implementation remains in this header so additional policy
// types can provide a separate wrapper and storage operations. Perimortem
// provides Thread, Global and Managed wrappers.
//
// The policy extension contract defines storage and key lifetime transitions
// through the following required operations:
//
//  auto allocate(Count requested_bytes) -> Allocation<U8>
//
//    Returns one writable allocation containing at least `requested_bytes`. Its
//    address satisfies the alignment of U32 and `key_type`. Set stores the
//    control hashes and aligned keys in that single buffer until `release`.
//
//  auto construct(key_type* destination, factory_type factory) -> key_type&
//
//    Establishes one current key at `destination` from exactly one call to
//    `factory`. The destination contains no current key before this operation.
//
//  auto relocate(key_type* destination, key_type& source) -> void
//
//    Establishes the current key in a replacement allocation. The source
//    remains accessible until Set subsequently passes it to `retire`.
//
//  auto retire(key_type* keys, Count count) -> void
//
//    Excludes the current key range from the Set without releasing its
//    containing allocation. The policy applies its key lifetime rule to that
//    range.
//
//  auto release(U8* data) -> void
//
//    Detaches the single allocation after every current key has been retired.
//    It accepts nullptr when the Set has no allocation.
//
// Policies can impose stronger allocation and lifetime invariants through these
// operations without changing the shared Set contract.
//
// The empty representation is guaranteed to be allocation free.
template <typename policy_type, typename key_type>
class Set {
 public:
  auto ensure_capacity(Count requested_capacity) -> void {
    constexpr U32 initial_bucket_count = 16;

    if (requested_capacity == 0) {
      return;
    }

    if (requested_capacity <= capacity_for_bucket_count(bucket_count)) {
      return;
    }

    U32 requested_buckets =
        bucket_count == 0 ? initial_bucket_count : bucket_count;
    while (requested_capacity > capacity_for_bucket_count(requested_buckets)) {
      if (requested_buckets == U32(1) << 31) {
        __builtin_trap();
      }

      requested_buckets <<= 1;
    }

    grow(requested_buckets);
  }

  // Inserts a missing key and returns True. An existing key leaves the Set
  // unchanged and returns False.
  auto insert(const key_type& key) -> Bool
    requires(__is_constructible(key_type, const key_type&))
  {
    return insert_key(key, [&key] { return key_type(key); });
  }

  // Move constructs a missing key in the Set. Duplicate detection completes
  // before the move, so an existing key leaves the supplied key unchanged and
  // returns False. When the key is missing, the operation moves the supplied
  // key into the Set and returns True.
  auto insert(key_type&& key) -> Bool
    requires(__is_constructible(key_type, key_type &&))
  {
    return insert_key(key, [&key] { return key_type(Core::Data::take(key)); });
  }

  // Removal marks its bucket as a sentinel so later probes continue to keys
  // displaced by the removed key. A missing key returns False.
  auto remove(const key_type& key) -> Bool {
    const Count bucket = find_bucket(key, get_hash(key));
    if (bucket == Count(-1) || !is_occupied(hashes[bucket])) {
      return False;
    }

    get_policy().retire(keys + bucket, 1);
    mark_sentinel(bucket);

    size--;
    return True;
  }

  // Removes every current key while retaining table capacity for reuse.
  // `reset()` returns the Set to its empty representation when retained
  // capacity is no longer useful.
  auto clear() -> void {
    if (!hashes) {
      return;
    }

    for (Count bucket = 0; bucket < bucket_count; bucket++) {
      if (is_occupied(hashes[bucket])) {
        get_policy().retire(keys + bucket, 1);
      }

      hashes[bucket] = 0;
    }

    size = 0;
    active_buckets = 0;
  }

  // Returns the Set to its empty representation. The policy decides when
  // storage detached by this operation can be reclaimed.
  auto reset() -> void { reset_storage(); }

  // The returned reference designates the stored key until a call to
  // `ensure_capacity()`, `insert()`, `remove()`, `clear()`, or `reset()`.
  auto find(const key_type& key) const
      -> Core::Storage::Option<const key_type&> {
    const Count bucket = find_bucket(key, get_hash(key));
    if (bucket == Count(-1) || !is_occupied(hashes[bucket])) {
      return {};
    }

    return keys[bucket];
  }

  auto contains(const key_type& key) const -> Bool { return bool(find(key)); }

  // Visits every current key in unspecified order. The visitor receives each
  // key as `const key_type&`, preventing mutation that would invalidate its
  // stored control hash. A structural Set operation ends the supplied
  // reference. If the visitor performs one, the remaining traversal may skip or
  // revisit keys in the resulting table.
  template <typename visitor_type>
  auto visit(visitor_type visitor) const -> void {
    for (Count bucket = 0; bucket < bucket_count; bucket++) {
      if (is_occupied(hashes[bucket])) {
        const key_type& key = keys[bucket];
        visitor(key);
      }
    }
  }

  constexpr auto get_size() const -> Count { return size; }
  constexpr auto get_capacity() const -> Count {
    return capacity_for_bucket_count(bucket_count);
  }

  constexpr auto is_empty() const -> Bool { return size == 0; }

 protected:
  constexpr Set() = default;
  Set(const Set&) = delete;
  Set(Set&&) = delete;
  auto operator=(const Set&) -> Set& = delete;
  auto operator=(Set&&) -> Set& = delete;
  constexpr ~Set() = default;

  auto copy_from(const Set& rhs) -> void
    requires(__is_constructible(key_type, const key_type&))
  {
    if (rhs.size == 0) {
      reset_storage();
      return;
    }

    ensure_capacity(rhs.size);
    for (Count bucket = 0; bucket < rhs.bucket_count; bucket++) {
      if (is_occupied(rhs.hashes[bucket])) {
        insert(rhs.keys[bucket]);
      }
    }
  }

  constexpr auto take(Set&& rhs) -> void {
    hashes = rhs.hashes;
    keys = rhs.keys;
    bucket_count = rhs.bucket_count;
    active_buckets = rhs.active_buckets;
    size = rhs.size;
    rhs.hashes = nullptr;
    rhs.keys = nullptr;
    rhs.bucket_count = 0;
    rhs.active_buckets = 0;
    rhs.size = 0;
  }

  auto reset_storage() -> void {
    clear();
    get_policy().release(Core::Data::cast<U8>(hashes));
    hashes = nullptr;
    keys = nullptr;
    bucket_count = 0;
    active_buckets = 0;
  }

  static constexpr auto allocation_size(Count capacity) -> Count {
    const Count offset = key_offset(capacity);
    if (offset == Count(-1) ||
        capacity > (Count(-1) - offset) / sizeof(key_type)) {
      return Count(-1);
    }

    return offset + sizeof(key_type) * capacity;
  }

 private:
  constexpr auto get_policy() -> policy_type& {
    return *Core::Data::cast<policy_type>(this);
  }

  static constexpr auto capacity_for_bucket_count(U32 count) -> Count {
    return Count(count) - count / 10 - (count % 10 != 0);
  }

  constexpr auto requires_rehash() const -> Bool {
    return Count(active_buckets) * 10 > Count(bucket_count) * 9;
  }

  template <typename factory_type>
  auto insert_key(const key_type& key, factory_type factory) -> Bool {
    if (bucket_count == 0) {
      ensure_capacity(1);
    }

    const U32 hash = get_hash(key);
    const Count bucket = find_bucket(key, hash);
    if (is_occupied(hashes[bucket])) {
      return False;
    }

    construct_key(bucket, hash, Core::Data::take(factory));
    if (requires_rehash()) {
      U32 requested_buckets = bucket_count;
      if (Count(size) * 10 > Count(bucket_count) * 9) {
        if (requested_buckets == U32(1) << 31) {
          __builtin_trap();
        }

        requested_buckets <<= 1;
      }

      grow(requested_buckets);
    }

    return True;
  }

  template <typename factory_type>
  auto construct_key(Count bucket, U32 hash, factory_type factory) -> void {
    // Reusing a sentinel does not consume another terminating empty control.
    const Bool activates_bucket = hashes[bucket] == 0;
    get_policy().construct(keys + bucket, Core::Data::take(factory));
    hashes[bucket] = mark_occupied(hash);
    size++;
    if (activates_bucket) {
      active_buckets++;
    }
  }

  auto relocate_key(key_type* destination, key_type& source) -> void {
    // Byte copying begins the lifetime of a trivial key in raw table storage
    // without adding constructor overhead to scalar rehashing. Nontrivial keys
    // delegate their lifetime transition to the policy.
    if constexpr (__is_trivially_copyable(key_type)) {
      memcpy(destination, &source, sizeof(key_type));
    } else {
      get_policy().relocate(destination, source);
    }
  }

  auto find_bucket(const key_type& key, U32 hash) const -> Count {
    if (bucket_count == 0) {
      return Count(-1);
    }

    const U32 control_hash = mark_occupied(hash);
    Count bucket = compute_bucket_index(hash);
    Count first_sentinel = Count(-1);

    // A sentinel can accept insertion, but lookup must continue in case the key
    // appears later in the probe chain. A genuinely empty bucket terminates the
    // search and returns the earliest reusable position.
    while (True) {
      const U32 stored_hash = hashes[bucket];
      if (stored_hash == 0) {
        return first_sentinel == Count(-1) ? bucket : first_sentinel;
      }

      if (is_occupied(stored_hash) && stored_hash == control_hash &&
          keys[bucket] == key) {
        return bucket;
      }

      if (!is_occupied(stored_hash) && first_sentinel == Count(-1)) {
        first_sentinel = bucket;
      }

      bucket = (bucket + 1) & (bucket_count - 1);
    }
  }

  auto grow(U32 new_bucket_count) -> void {
    U32* old_hashes = hashes;
    key_type* old_keys = keys;
    const U32 old_bucket_count = bucket_count;

    auto allocation = get_policy().allocate(allocation_size(new_bucket_count));
    hashes = Core::Data::cast<U32>(allocation.get_data());
    keys = Core::Data::cast<key_type>(
        allocation.get_data() + key_offset(new_bucket_count));
    bucket_count = new_bucket_count;
    active_buckets = 0;
    size = 0;
    for (Count bucket = 0; bucket < bucket_count; bucket++) {
      hashes[bucket] = 0;
    }

    for (Count bucket = 0; bucket < old_bucket_count; bucket++) {
      if (!is_occupied(old_hashes[bucket])) {
        continue;
      }

      const Count destination =
          find_bucket(old_keys[bucket], old_hashes[bucket]);
      relocate_key(keys + destination, old_keys[bucket]);
      hashes[destination] = old_hashes[bucket];
      active_buckets++;
      size++;
      get_policy().retire(old_keys + bucket, 1);
    }

    get_policy().release(Core::Data::cast<U8>(old_hashes));
  }

  static constexpr auto key_offset(Count capacity) -> Count {
    if (capacity > Count(-1) / sizeof(U32)) {
      return Count(-1);
    }

    const Count controls = sizeof(U32) * capacity;
    if (controls > Count(-1) - (alignof(key_type) - 1)) {
      return Count(-1);
    }

    return Core::Data::align<alignof(key_type)>(controls);
  }

  constexpr auto compute_bucket_index(U32 hash) const -> Count {
    return Count(hash & (bucket_count - 1));
  }

  static constexpr auto mark_occupied(U32 hash) -> U32 {
    return hash | U32(0x80000000);
  }

  constexpr auto mark_sentinel(Count bucket) -> void {
    hashes[bucket] = U32(0x7FFFFFFF);
  }

  static constexpr auto is_occupied(U32 hash) -> Bool {
    return (hash & U32(0x80000000)) != 0;
  }

  static auto get_hash(const key_type& key) -> U32 {
    return U32(Core::Hash(key).get_value());
  }

  U32* hashes = nullptr;
  key_type* keys = nullptr;
  U32 bucket_count = 0;
  U32 active_buckets = 0;
  U32 size = 0;
};

}  // namespace Perimortem::Memory::Container
