// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/dynamic/bytes.hpp"

#include "perimortem/core/bibliotheca.hpp"
#include "perimortem/core/data.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/null_terminated.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;

Dynamic::Bytes::Bytes(Count reserved_capacity) {
  ensure_capacity(reserved_capacity);
}

Dynamic::Bytes::Bytes(const View::Bytes view) : Bytes(view.get_size()) {
  size = view.get_size();
  if (!view.is_empty()) {
    Data::copy(source_block, view.get_data(), view.get_size());
  }
}

Dynamic::Bytes::Bytes(const Bytes& rhs) : Bytes(rhs.get_view()) {}

Dynamic::Bytes::Bytes(Bytes&& rhs)
    : source_block(rhs.source_block), size(rhs.size), capacity(rhs.capacity) {
  rhs.source_block = nullptr;
  rhs.size = 0;
  rhs.capacity = 0;
}

Dynamic::Bytes::~Bytes() {
  reset();
}

auto Dynamic::Bytes::operator=(View::Bytes view) -> Bytes& {
  proxy(view);
  return *this;
}

auto Dynamic::Bytes::operator=(const Bytes& rhs) -> Bytes& {
  if (this == &rhs) {
    return *this;
  }

  proxy(rhs.get_view());
  return *this;
}

auto Dynamic::Bytes::operator=(Bytes&& rhs) -> Bytes& {
  if (this == &rhs) {
    return *this;
  }

  reset();
  source_block = rhs.source_block;
  size = rhs.size;
  capacity = rhs.capacity;
  rhs.source_block = nullptr;
  rhs.size = 0;
  rhs.capacity = 0;
  return *this;
}

auto Dynamic::Bytes::append(U8 byte) -> void {
  ensure_capacity(size + 1);
  source_block[size++] = byte;
}

auto Dynamic::Bytes::append(U8 byte, Count amount) -> void {
  if (amount == 0) {
    return;
  }

  if (amount > Count(-1) - size) {
    Diagnostics::Log::fatal(
        "Dynamic Bytes append exceeds the addressable size."_view);
  }

  ensure_capacity(size + amount);
  Data::set(source_block + size, byte, amount);
  size += amount;
}

auto Dynamic::Bytes::concat(View::Bytes view) -> void {
  if (view.is_empty()) {
    return;
  }

  if (view.get_size() > Count(-1) - get_size()) {
    Diagnostics::Log::fatal(
        "Dynamic Bytes concatenation exceeds the addressable size."_view);
  }

  Count required_size = size + view.get_size();
  if (required_size <= capacity) {
    memmove(source_block + size, view.get_data(), view.get_size());
  } else {
    auto allocation = Bibliotheca::check_out(required_size);
    if (size) {
      Data::copy(allocation.ptr, source_block, size);
    }

    // The appended view may borrow this allocation. Finish reading it before
    // returning the old block to Bibliotheca for reuse.
    Data::copy(allocation.ptr + size, view.get_data(), view.get_size());
    if (source_block) {
      Bibliotheca::remit(source_block);
    }

    source_block = allocation.ptr;
    capacity = allocation.capacity;
  }

  size = required_size;
}

auto Dynamic::Bytes::proxy(View::Bytes view) -> void {
  if (view.is_empty()) {
    reset();
    return;
  }

  if (view.get_size() <= capacity) {
    // A slice of this buffer can overlap its destination.
    memmove(source_block, view.get_data(), view.get_size());
  } else {
    auto allocation = Bibliotheca::check_out(view.get_size());
    Data::copy(allocation.ptr, view.get_data(), view.get_size());
    reset();
    source_block = allocation.ptr;
    capacity = allocation.capacity;
  }

  size = view.get_size();
}

auto Dynamic::Bytes::set(U8 target) -> void {
  if (is_empty()) {
    return;
  }

  Data::set(source_block, target, size);
}

auto Dynamic::Bytes::convert(U8 source, U8 target) -> void {
  for (Count index = 0; index < size; index++) {
    if (source_block[index] == source) {
      source_block[index] = target;
    }
  }
}

auto Dynamic::Bytes::slice(Count start, Count size) const -> View::Bytes {
  return get_view().slice(start, size);
}

auto Dynamic::Bytes::resize(Count new_size) -> void {
  ensure_capacity(new_size);
  size = new_size;
}

auto Dynamic::Bytes::forgetful_resize(Count required_size) -> void {
  if (required_size <= capacity && required_size > (capacity >> 1)) {
    size = required_size;
    return;
  }

  reset();
  ensure_capacity(required_size);
  size = required_size;
}

auto Dynamic::Bytes::shrink(Count bytes_to_remove) -> void {
  if (bytes_to_remove >= size) {
    clear();
    return;
  }

  if (bytes_to_remove == 0) {
    return;
  }

  Count remaining = size - bytes_to_remove;
  memmove(source_block, source_block + bytes_to_remove, remaining);
  size = remaining;
}

auto Dynamic::Bytes::operator[](Count index) const -> U8 {
  return get_view()[index];
}

auto Dynamic::Bytes::at(Count index) const -> U8 {
  return get_view()[index];
}

auto Dynamic::Bytes::clear() -> void {
  size = 0;
}

auto Dynamic::Bytes::reset() -> void {
  if (source_block) {
    Bibliotheca::remit(source_block);
    source_block = nullptr;
  }

  size = 0;
  capacity = 0;
}

auto Dynamic::Bytes::ensure_capacity(Count required_size) -> void {
  if (required_size <= capacity) {
    return;
  }

  auto allocation = Bibliotheca::check_out(required_size);
  if (source_block) {
    // Shrinking only changes the visible size. Preserve the complete buffer
    // so a later resize can expose those bytes again, including after growth.
    Data::copy(allocation.ptr, source_block, capacity);
    Bibliotheca::remit(source_block);
  }

  source_block = allocation.ptr;
  capacity = allocation.capacity;
}
