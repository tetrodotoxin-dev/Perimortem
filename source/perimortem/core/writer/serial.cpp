// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/writer/serial.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/scalar.hpp"

using namespace Perimortem::Core;

constexpr auto native_endian = Data::ByteOrder::Native;
constexpr auto stream_endian = Data::ByteOrder::Little;

constexpr auto negate_flag = 0x10;
constexpr auto blob_flag = 0x20;

constexpr auto
    write_value(Access::Bytes target, Count& cursor, S64 value, U8 flags)
        -> Bool {
  // Serial stores negative values as a magnitude plus a sign flag so their
  // payload width matches positive values.
  if (value < 0) {
    flags |= negate_flag;
  }

  U64 magnitude = Scalar::magnitude<U64>(value);

  // The low type nibble records payload width. Select the narrowest width that
  // preserves the magnitude so small values and blob lengths do not carry
  // unused leading bytes.
  Count encoding_size = 8;
  if (magnitude <= U8(-1)) {
    encoding_size = 1;
  } else if (magnitude <= U16(-1)) {
    encoding_size = 2;
  } else if (magnitude <= U32(-1)) {
    encoding_size = 4;
  }

  constexpr Count type_size = 1;
  if (cursor > target.get_size() ||
      encoding_size > target.get_size() - cursor ||
      type_size > target.get_size() - cursor - encoding_size) [[unlikely]] {
    return False;
  }

  auto data = target.get_data();
  data[cursor++] = U8(encoding_size | flags);
  switch (encoding_size) {
  case 1:
    data[cursor] = U8(magnitude);
    break;
  case 2:
    Data::copy(
        data + cursor,
        Data::ensure_endian<native_endian, stream_endian, U16>(magnitude));
    break;
  case 4:
    Data::copy(
        data + cursor,
        Data::ensure_endian<native_endian, stream_endian, U32>(magnitude));
    break;
  case 8:
    Data::copy(
        data + cursor,
        Data::ensure_endian<native_endian, stream_endian, U64>(magnitude));
    break;
  }

  cursor += encoding_size;
  return True;
}

constexpr auto write_blob(
    Access::Bytes target,
    Count& cursor,
    View::Bytes source) -> Bool {
  if (!write_value(target, cursor, source.get_size(), blob_flag)) {
    return False;
  }

  if (cursor > target.get_size() ||
      source.get_size() > target.get_size() - cursor) {
    return False;
  }

  auto data = target.get_data();
  Data::copy(data + cursor, source.get_data(), source.get_size());
  cursor += source.get_size();
  return True;
}

auto Writer::Serial::set_pointer(Count location) -> void {
  cursor = location < source.get_size() ? location : source.get_size();
}

auto Writer::Serial::operator<<(const U64 value) -> Writer::Serial& {
  valid_state &= write_value(source, cursor, value, 0x00);
  return *this;
}

auto Writer::Serial::operator<<(const View::Bytes blob) -> Writer::Serial& {
  // Write type information
  valid_state &= write_blob(source, cursor, blob);
  return *this;
}
