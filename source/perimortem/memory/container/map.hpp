// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/data.hpp"
#include "perimortem/core/hash.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/core/storage/option.hpp"

namespace Perimortem::Memory::Container {

// Map represents unordered key value pairs for lookup by key. The `policy_type`
// implementation establishes allocation and key and value lifetime behavior
// without changing lookup or duplicate key semantics.
//
// Map uses open addressing with linear probing. One allocation contains packed
// U32 control hashes followed by an aligned key value pair range. An occupied
// control sets its high bit. Zero terminates a missing key probe, while a
// sentinel left by removal keeps the probe active and can be reused by a later
// insertion. This rejects most hashes without loading key or value storage.
// Rehashing at 90% active controls keeps a terminating zero available and
// purges accumulated sentinels.
//
// Each key must maintain the hash and equality characteristics it had when it
// was inserted for the lifetime of the key value pair. Changing either
// characteristic through external state is undefined behavior because lookup
// follows the stored control hash.
//
// Every returned value reference continues to designate the same current value
// until `ensure_capacity()`, `clear()`, `reset()`, `insert()`, `assign()`,
// `remove()`, or `find_or_create()` is called.
// Direct value mutation through a current reference is supported.
//
// `get_capacity()` reports the total number of key value pairs the Map can
// hold before crossing the maximum load. The 90% maximum load belongs to the
// container capability, so every policy derives the same internal bucket
// count. Calling `ensure_capacity(get_capacity())` never reallocates. The high
// control bit records occupancy, leaving 31 bits for a power of two bucket
// index. A capacity requiring more than 2^31 buckets terminates instead of
// wrapping that index. The first nonzero capacity request allocates 16 buckets
// and later growth doubles the bucket count.
//
// The complete implementation remains in this header so additional policy
// types can provide a separate wrapper and storage operations. Perimortem
// provides Thread, Global and Managed wrappers.
//
// The policy extension contract defines storage, key and value lifetime
// transitions through the following required operations:
//
//  auto allocate(Count requested_bytes) -> Allocation<U8>
//
//    Returns one writable allocation containing at least `requested_bytes`. Its
//    address satisfies the alignment of U32 and `Bucket`. Map stores the
//    control hashes and aligned key value pairs in that single buffer until
//    `release`.
//
//  auto construct(Bucket* destination, factory_type factory) -> Bucket&
//
//    Establishes one current key value pair at `destination` from exactly one
//    call to `factory`. The destination contains no current pair before this
//    operation.
//
//  auto relocate(Bucket* destination, Bucket& source) -> void
//
//    Establishes the current key value pair in a replacement allocation. The
//    source remains accessible until Map subsequently passes it to `retire`.
//
//  auto replace(value_type* destination, factory_type factory) -> value_type&
//
//    Produces the replacement before invalidating the current value because
//    `factory` may read from that value. The returned reference identifies the
//    new current value at `destination`.
//
//  auto retire(Bucket* pairs, Count count) -> void
//
//    Excludes the current key value pair range from the Map without releasing
//    its containing allocation. The policy applies its lifetime rules to that
//    range.
//
//  auto release(U8* data) -> void
//
//    Detaches the single allocation after every current key value pair has been
//    retired. It accepts nullptr when the Map has no allocation.
//
// Policies can impose stronger allocation and lifetime invariants through these
// operations without changing the shared Map contract.
//
// The empty representation is guaranteed to be allocation free.
template <typename policy_type, typename key_type, typename value_type>
class Map {
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

  // Removes every current key value pair while retaining table capacity for
  // reuse. `reset()` returns the Map to its empty representation when retained
  // capacity is no longer useful.
  auto clear() -> void {
    if (!hashes) {
      return;
    }

    for (Count bucket = 0; bucket < bucket_count; bucket++) {
      if (is_occupied(hashes[bucket])) {
        get_policy().retire(buckets + bucket, 1);
      }

      hashes[bucket] = 0;
    }

    size = 0;
    active_buckets = 0;
  }

  // Returns the Map to its empty representation. The policy decides when
  // storage detached by this operation can be reclaimed.
  auto reset() -> void { reset_storage(); }

  // Inserts a missing key by copying the supplied key and value. Lookup
  // completes before either copy, so an existing key leaves both arguments and
  // the Map unchanged and returns None.
  auto insert(const key_type& key, const value_type& value)
      -> Core::Storage::Option<value_type&>
    requires(
        __is_constructible(key_type, const key_type&) &&
        __is_constructible(value_type, const value_type&))
  {
    return insert_if_missing(
        key, [&key, &value] { return Bucket(key, value); });
  }

  // Inserts a missing key by copying the key and moving the supplied value.
  // Lookup completes before the move, so an existing key leaves the value
  // unchanged and returns None.
  auto insert(const key_type& key, value_type&& value)
      -> Core::Storage::Option<value_type&>
    requires(
        __is_constructible(key_type, const key_type&) &&
        __is_constructible(value_type, value_type &&))
  {
    return insert_if_missing(
        key, [&key, &value] { return Bucket(key, Core::Data::take(value)); });
  }

  // Copies a replacement value only when the key is already stored. A missing
  // key leaves the Map and supplied value unchanged and returns None.
  auto assign(const key_type& key, const value_type& value)
      -> Core::Storage::Option<value_type&>
    requires(__is_constructible(value_type, const value_type&))
  {
    const Count bucket = find_bucket(key, get_hash(key));
    if (bucket == Count(-1) || !is_occupied(hashes[bucket])) {
      return {};
    }

    return replace_value(buckets[bucket].value, value);
  }

  // Moves a replacement value only when the key is already stored. A missing
  // key leaves the Map and supplied value unchanged and returns None.
  auto assign(const key_type& key, value_type&& value)
      -> Core::Storage::Option<value_type&>
    requires(__is_constructible(value_type, value_type &&))
  {
    const Count bucket = find_bucket(key, get_hash(key));
    if (bucket == Count(-1) || !is_occupied(hashes[bucket])) {
      return {};
    }

    return replace_value_by_move(buckets[bucket].value, value);
  }

  // Returns the current value for `key`, or None when the Map does not contain
  // that key. The returned reference follows the Map reference lifetime
  // contract and permits direct value mutation.
  auto find(const key_type& key) -> Core::Storage::Option<value_type&> {
    const Count bucket = find_bucket(key, get_hash(key));
    if (bucket == Count(-1) || !is_occupied(hashes[bucket])) {
      return {};
    }

    return buckets[bucket].value;
  }

  auto find(const key_type& key) const
      -> Core::Storage::Option<const value_type&> {
    const Count bucket = find_bucket(key, get_hash(key));
    if (bucket == Count(-1) || !is_occupied(hashes[bucket])) {
      return {};
    }

    return buckets[bucket].value;
  }

  auto contains(const key_type& key) const -> Bool { return bool(find(key)); }

  // Visits every current key value pair in unspecified order. The visitor
  // receives each key as `const key_type&` and each value as `value_type&`, so
  // value mutation cannot invalidate the stored control hash. A structural Map
  // operation ends the supplied references. If the visitor performs one, the
  // remaining traversal may skip or revisit key value buckets in the resulting
  // table.
  template <typename visitor_type>
  auto visit(visitor_type visitor) -> void {
    for (Count bucket = 0; bucket < bucket_count; bucket++) {
      if (is_occupied(hashes[bucket])) {
        const key_type& key = buckets[bucket].key;
        visitor(key, buckets[bucket].value);
      }
    }
  }

  template <typename visitor_type>
  auto visit(visitor_type visitor) const -> void {
    for (Count bucket = 0; bucket < bucket_count; bucket++) {
      if (is_occupied(hashes[bucket])) {
        const key_type& key = buckets[bucket].key;
        const value_type& value = buckets[bucket].value;
        visitor(key, value);
      }
    }
  }

  // Removal marks its bucket as a sentinel so later probes continue to keys
  // displaced by the removed key. A missing key returns False.
  auto remove(const key_type& key) -> Bool {
    Count bucket = find_bucket(key, get_hash(key));
    if (bucket == Count(-1) || !is_occupied(hashes[bucket])) {
      return False;
    }

    get_policy().retire(buckets + bucket, 1);
    mark_sentinel(bucket);

    size--;
    return True;
  }

  // Returns the current value for `key`. When the key is missing, copies the
  // key and default constructs its value before any required rehash.
  auto find_or_create(const key_type& key) -> value_type&
    requires(
        __is_constructible(key_type, const key_type&) &&
        __is_constructible(value_type))
  {
    if (bucket_count == 0) {
      ensure_capacity(1);
    }

    const U32 hash = get_hash(key);
    Count bucket = find_bucket(key, hash);
    if (is_occupied(hashes[bucket])) {
      return buckets[bucket].value;
    }

    construct_bucket(
        bucket, hash, [&key] { return Bucket(key, value_type()); });

    bucket = rehash_after_insert(bucket);
    return buckets[bucket].value;
  }

  // Performs lookup without inserting a missing key. None reports that the key
  // is absent.
  auto operator[](const key_type& key) -> Core::Storage::Option<value_type&> {
    return find(key);
  }

  auto operator[](const key_type& key) const
      -> Core::Storage::Option<const value_type&> {
    return find(key);
  }

  constexpr auto get_size() const -> Count { return size; }
  constexpr auto get_capacity() const -> Count {
    return capacity_for_bucket_count(bucket_count);
  }

  constexpr auto is_empty() const -> Bool { return size == 0; }

 protected:
  struct Bucket {
    key_type key;
    value_type value;
  };

  constexpr Map() = default;
  Map(const Map&) = delete;
  Map(Map&&) = delete;
  auto operator=(const Map&) -> Map& = delete;
  auto operator=(Map&&) -> Map& = delete;
  constexpr ~Map() = default;

  auto copy_from(const Map& rhs) -> void
    requires(
        __is_constructible(key_type, const key_type&) &&
        __is_constructible(value_type, const value_type&))
  {
    if (rhs.size == 0) {
      reset_storage();
      return;
    }

    ensure_capacity(rhs.size);
    for (Count bucket = 0; bucket < rhs.bucket_count; bucket++) {
      if (is_occupied(rhs.hashes[bucket])) {
        insert(rhs.buckets[bucket].key, rhs.buckets[bucket].value);
      }
    }
  }

  constexpr auto take(Map&& rhs) -> void {
    hashes = rhs.hashes;
    buckets = rhs.buckets;
    bucket_count = rhs.bucket_count;
    active_buckets = rhs.active_buckets;
    size = rhs.size;
    rhs.hashes = nullptr;
    rhs.buckets = nullptr;
    rhs.bucket_count = 0;
    rhs.active_buckets = 0;
    rhs.size = 0;
  }

  auto reset_storage() -> void {
    clear();
    get_policy().release(Core::Data::cast<U8>(hashes));
    hashes = nullptr;
    buckets = nullptr;
    bucket_count = 0;
    active_buckets = 0;
  }

  static constexpr auto allocation_size(Count capacity) -> Count {
    const Count offset = pair_offset(capacity);
    if (offset == Count(-1) ||
        capacity > (Count(-1) - offset) / sizeof(Bucket)) {
      return Count(-1);
    }

    return offset + sizeof(Bucket) * capacity;
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

  // An insertion may rehash after publishing the new pair. Returning that
  // pair's relocated bucket lets `insert` and `find_or_create` return its
  // current value without probing for the key a second time. A same size rehash
  // purges sentinels, while the bucket count grows only when the live pairs
  // require more capacity.
  auto rehash_after_insert(Count inserted_bucket) -> Count {
    if (!requires_rehash()) {
      return inserted_bucket;
    }

    U32 requested_buckets = bucket_count;
    if (Count(size) * 10 > Count(bucket_count) * 9) {
      if (requested_buckets == U32(1) << 31) {
        __builtin_trap();
      }

      requested_buckets <<= 1;
    }

    return grow(requested_buckets, inserted_bucket);
  }

  template <typename factory_type>
  auto construct_bucket(Count bucket, U32 hash, factory_type factory) -> void {
    // Reusing a sentinel does not consume another terminating empty control.
    const Bool activates_bucket = hashes[bucket] == 0;
    get_policy().construct(buckets + bucket, Core::Data::take(factory));
    hashes[bucket] = mark_occupied(hash);
    size++;
    if (activates_bucket) {
      active_buckets++;
    }
  }

  template <typename factory_type>
  auto insert_if_missing(const key_type& key, factory_type factory)
      -> Core::Storage::Option<value_type&> {
    if (bucket_count == 0) {
      ensure_capacity(1);
    }

    const U32 hash = get_hash(key);
    Count bucket = find_bucket(key, hash);
    if (is_occupied(hashes[bucket])) {
      return {};
    }

    construct_bucket(bucket, hash, Core::Data::take(factory));
    bucket = rehash_after_insert(bucket);
    return buckets[bucket].value;
  }

  auto replace_value(value_type& destination, const value_type& source)
      -> value_type& {
    if (&destination == &source) {
      return destination;
    }

    return get_policy().replace(
        &destination, [&source] { return value_type(source); });
  }

  auto replace_value_by_move(value_type& destination, value_type& source)
      -> value_type& {
    if (&destination == &source) {
      return destination;
    }

    return get_policy().replace(&destination, [&source] {
      return value_type(Core::Data::take(source));
    });
  }

  auto relocate_bucket(Bucket* destination, Bucket& source) -> void {
    // Byte copying begins the lifetime of a trivial bucket in raw table storage
    // without adding constructor overhead to scalar rehashing. Nontrivial
    // buckets delegate their lifetime transition to the policy.
    if constexpr (__is_trivially_copyable(Bucket)) {
      memcpy(destination, &source, sizeof(Bucket));
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
          buckets[bucket].key == key) {
        return bucket;
      }

      if (!is_occupied(stored_hash) && first_sentinel == Count(-1)) {
        first_sentinel = bucket;
      }

      bucket = (bucket + 1) & (bucket_count - 1);
    }
  }

  auto grow(U32 new_bucket_count, Count tracked_bucket = Count(-1)) -> Count {
    U32* old_hashes = hashes;
    Bucket* old_buckets = buckets;
    const U32 old_bucket_count = bucket_count;

    auto allocation = get_policy().allocate(allocation_size(new_bucket_count));
    hashes = Core::Data::cast<U32>(allocation.get_data());
    buckets = Core::Data::cast<Bucket>(
        allocation.get_data() + pair_offset(new_bucket_count));
    bucket_count = new_bucket_count;
    active_buckets = 0;
    size = 0;
    for (Count bucket = 0; bucket < bucket_count; bucket++) {
      hashes[bucket] = 0;
    }

    Count relocated_bucket = Count(-1);
    for (Count bucket = 0; bucket < old_bucket_count; bucket++) {
      if (!is_occupied(old_hashes[bucket])) {
        continue;
      }

      const Count destination =
          find_bucket(old_buckets[bucket].key, old_hashes[bucket]);
      relocate_bucket(buckets + destination, old_buckets[bucket]);
      hashes[destination] = old_hashes[bucket];
      active_buckets++;
      size++;
      get_policy().retire(old_buckets + bucket, 1);

      if (bucket == tracked_bucket) {
        relocated_bucket = destination;
      }
    }

    get_policy().release(Core::Data::cast<U8>(old_hashes));
    return relocated_bucket;
  }

  static constexpr auto pair_offset(Count capacity) -> Count {
    if (capacity > Count(-1) / sizeof(U32)) {
      return Count(-1);
    }

    const Count controls = sizeof(U32) * capacity;
    if (controls > Count(-1) - (alignof(Bucket) - 1)) {
      return Count(-1);
    }

    return Core::Data::align<alignof(Bucket)>(controls);
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
  Bucket* buckets = nullptr;
  U32 bucket_count = 0;
  U32 active_buckets = 0;
  U32 size = 0;
};

}  // namespace Perimortem::Memory::Container
