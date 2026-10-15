// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/container/bytes.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/core/scalar.hpp"
#include "perimortem/memory/global/bytes.hpp"
#include "perimortem/memory/managed/bytes.hpp"
#include "perimortem/memory/thread/bytes.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;

template <typename policy_type>
auto Container::Bytes<policy_type>::append(U8 byte) -> void {
  ensure_capacity(size + 1);
  source_block[size++] = byte;
}

template <typename policy_type>
auto Container::Bytes<policy_type>::append(U8 byte, Count amount) -> void {
  if (amount == 0) {
    return;
  }

  if (amount > Count(-1) - size) {
    Diagnostics::Log::fatal("Bytes append exceeds the addressable size."_view);
  }

  ensure_capacity(size + amount);
  Data::set(source_block + size, byte, amount);
  size += amount;
}

template <typename policy_type>
auto Container::Bytes<policy_type>::concat(View::Bytes view) -> void {
  if (view.is_empty()) {
    return;
  }

  if (view.get_size() > Count(-1) - size) {
    Diagnostics::Log::fatal(
        "Bytes concatenation exceeds the addressable size."_view);
  }

  const Count required_size = size + view.get_size();
  if (required_size <= capacity) {
    // A View over the current values ends no later than the append
    // destination begins. Those source and destination ranges cannot overlap.
    Data::copy(source_block + size, view.get_data(), view.get_size());
    size = required_size;
    return;
  }

  const Count requested_capacity =
      policy_type::next_capacity(capacity, required_size);
  auto allocation = get_policy().allocate(requested_capacity);
  if (size) {
    Data::copy(allocation.get_data(), source_block, size);
  }

  // `ensure_capacity` could release storage addressed by `view`. Build the new
  // block directly and release the old source only after the append completes.
  Data::copy(allocation.get_data() + size, view.get_data(), view.get_size());
  policy_type::release(source_block);
  source_block = allocation.get_data();
  capacity = allocation.get_capacity();
  size = required_size;
}

template <typename policy_type>
auto Container::Bytes<policy_type>::copy(View::Bytes view) -> void {
  if (view.is_empty()) {
    reset();
    return;
  }

  // `view` may address this allocation. Retained storage uses `memmove` for
  // overlap, while growth fills the new block before releasing the old source.
  if (view.get_size() <= capacity) {
    memmove(source_block, view.get_data(), view.get_size());
  } else {
    const Count requested_capacity =
        policy_type::next_capacity(capacity, view.get_size());
    auto allocation = get_policy().allocate(requested_capacity);
    Data::copy(allocation.get_data(), view.get_data(), view.get_size());
    policy_type::release(source_block);
    source_block = allocation.get_data();
    capacity = allocation.get_capacity();
  }

  size = view.get_size();
}

template <typename policy_type>
auto Container::Bytes<policy_type>::set(U8 value) -> void {
  if (!is_empty()) {
    Data::set(source_block, value, size);
  }
}

template <typename policy_type>
auto Container::Bytes<policy_type>::operator[](Count index) -> Option<U8&> {
  if (index >= size) {
    return {};
  }

  return source_block[index];
}

template <typename policy_type>
auto Container::Bytes<policy_type>::slice(Count start, Count size) const
    -> View::Bytes {
  return get_view().slice(start, size);
}

template <typename policy_type>
auto Container::Bytes<policy_type>::resize(Count new_size) -> void {
  const Count old_size = size;
  ensure_capacity(new_size);
  if (new_size > old_size) {
    Data::set(source_block + old_size, 0, new_size - old_size);
  }

  size = new_size;
}

template <typename policy_type>
auto Container::Bytes<policy_type>::forgetful_resize(Count new_size) -> void {
  if (new_size <= capacity && new_size > (capacity >> 1)) {
    size = new_size;
    return;
  }

  reset();
  ensure_capacity(new_size);
  size = new_size;
}

template <typename policy_type>
auto Container::Bytes<policy_type>::reframe(S64 start, Count new_size) -> void {
  if (new_size == 0) {
    clear();
    return;
  }

  const Count old_size = size;
  Count source_start = 0;
  Count destination_start = 0;
  if (start < 0) {
    destination_start = Scalar::min(Scalar::magnitude<Count>(start), new_size);
  } else {
    source_start = Scalar::min(Count(start), old_size);
  }

  const Count source_size = old_size - source_start;
  const Count destination_size = new_size - destination_start;
  const Count copied_size = Scalar::min(source_size, destination_size);

  ensure_capacity(new_size);
  if (copied_size) {
    // Move retained values before clearing padding because a negative start can
    // shift source values into the range that becomes leading zeros.
    memmove(
        source_block + destination_start, source_block + source_start,
        copied_size);
  }

  if (destination_start) {
    Data::set(source_block, 0, destination_start);
  }

  const Count trailing_start = destination_start + copied_size;
  if (trailing_start < new_size) {
    Data::set(source_block + trailing_start, 0, new_size - trailing_start);
  }

  size = new_size;
}

template <typename policy_type>
auto Container::Bytes<policy_type>::operator[](Count index) const
    -> Option<const U8&> {
  if (index >= size) {
    return {};
  }

  return source_block[index];
}

template <typename policy_type>
auto Container::Bytes<policy_type>::clear() -> void {
  size = 0;
}

template <typename policy_type>
auto Container::Bytes<policy_type>::reset() -> void {
  if (source_block) {
    policy_type::release(source_block);
  }

  source_block = nullptr;
  size = 0;
  capacity = 0;
}

template <typename policy_type>
auto Container::Bytes<policy_type>::ensure_capacity(Count required_size)
    -> void {
  if (required_size <= capacity) {
    return;
  }

  const Count requested_capacity =
      policy_type::next_capacity(capacity, required_size);
  auto allocation = get_policy().allocate(requested_capacity);
  if (source_block) {
    // Only values below the current size belong to the Bytes object. Remaining
    // allocation contents are not copied into replacement storage.
    Data::copy(allocation.get_data(), source_block, size);
    policy_type::release(source_block);
  }

  source_block = allocation.get_data();
  capacity = allocation.get_capacity();
}

// Explicit instantiation closes the capability over Perimortem's supported
// policies. Exported member declarations publish the resulting symbols while
// the template implementation remains private to the library.
template class Container::Bytes<Thread::Bytes>;
template class Container::Bytes<Global::Bytes>;
template class Container::Bytes<Managed::Bytes>;
