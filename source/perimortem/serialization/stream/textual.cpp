// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/serialization/stream/textual.hpp"

#include "perimortem/core/writer/textual.hpp"
#include "perimortem/memory/managed/bytes.hpp"
#include "perimortem/memory/thread/bytes.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::Serialization;

// Scalar textual writers require less than 64 contiguous bytes for one value.
// Reserving that fixed window lets a write cross the storage boundary without a
// partial result. The storage shrinks to the final cursor afterward, while raw
// byte Views bypass the window through `concat`.
constexpr Count write_window = 64;

template <typename storage_type, typename value_type>
static auto write(storage_type& storage, value_type value) -> void {
  Count start = storage.get_size();
  storage.resize(start + write_window);

  // Writer receives the complete window as writable capacity. Shrink to its
  // final cursor so unused reservation does not become stream content.
  Writer::Textual writer(storage.get_access().slice(start, write_window));
  writer << value;
  storage.resize(start + writer.get_location());
}

template <typename storage_type>
auto Stream::Textual<storage_type>::operator<<(Bool flag) -> Textual& {
  write(storage, flag);
  return *this;
}

template <typename storage_type>
auto Stream::Textual<storage_type>::operator<<(U8 value) -> Textual& {
  write(storage, value);
  return *this;
}

template <typename storage_type>
auto Stream::Textual<storage_type>::operator<<(U16 value) -> Textual& {
  write(storage, value);
  return *this;
}

template <typename storage_type>
auto Stream::Textual<storage_type>::operator<<(U32 value) -> Textual& {
  write(storage, value);
  return *this;
}

template <typename storage_type>
auto Stream::Textual<storage_type>::operator<<(U64 value) -> Textual& {
  write(storage, value);
  return *this;
}

template <typename storage_type>
auto Stream::Textual<storage_type>::operator<<(S8 value) -> Textual& {
  write(storage, value);
  return *this;
}

template <typename storage_type>
auto Stream::Textual<storage_type>::operator<<(S16 value) -> Textual& {
  write(storage, value);
  return *this;
}

template <typename storage_type>
auto Stream::Textual<storage_type>::operator<<(S32 value) -> Textual& {
  write(storage, value);
  return *this;
}

template <typename storage_type>
auto Stream::Textual<storage_type>::operator<<(S64 value) -> Textual& {
  write(storage, value);
  return *this;
}

template <typename storage_type>
auto Stream::Textual<storage_type>::operator<<(R32 value) -> Textual& {
  write(storage, value);
  return *this;
}

template <typename storage_type>
auto Stream::Textual<storage_type>::operator<<(R64 value) -> Textual& {
  write(storage, value);
  return *this;
}

template <typename storage_type>
auto Stream::Textual<storage_type>::operator<<(View::Bytes raw) -> Textual& {
  storage.concat(raw);
  return *this;
}

// The operator definitions live here. Each supported storage variant needs an
// exported instantiation for consumers that link the shared library.
template class EXPORTED(PERIMORTEM) Stream::Textual<Thread::Bytes>;
template class EXPORTED(PERIMORTEM) Stream::Textual<Managed::Bytes>;
