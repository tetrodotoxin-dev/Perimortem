// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/reader/serial.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/diagnostics/message.hpp"
#include "perimortem/core/view/null_terminated.hpp"

using namespace Perimortem::Core;

constexpr auto native_endian = Data::ByteOrder::Native;
constexpr auto stream_endian = Data::ByteOrder::Little;

constexpr auto negate_flag = 0x10;
constexpr auto blob_flag = 0x20;

auto Reader::Serial::read() -> Value {
  if (!has_content()) {
    Diagnostics::Message<128> error_message(Diagnostics::Log::Level::Error);
    error_message
        << "Serial read overran data buffer while reading type at byte location "_view
        << cursor << ". source_size="_view << source.get_size();

    cursor = Count(-1);
    return Value();
  }

  auto data = source.get_data();
  auto byte_flag = data[cursor++];
  U8 encoded_size = byte_flag & 0xF;
  if (cursor + encoded_size > source.get_size()) {
    Diagnostics::Message<128> error_message(Diagnostics::Log::Level::Error);
    error_message
        << "Serial read overran data buffer while reading value at byte location "_view
        << cursor << ". source_size="_view << source.get_size()
        << " encoded_size="_view << encoded_size;

    cursor = Count(-1);
    return Value();
  }

  U64 magnitude;
  switch (encoded_size) {
  case 1:
    magnitude = source[cursor];
    break;
  case 2: {
    U16 actual_bytes;
    memcpy(&actual_bytes, data + cursor, sizeof(U16));
    magnitude = Data::ensure_endian<stream_endian, native_endian>(actual_bytes);
    break;
  }

  case 4: {
    U32 actual_bytes;
    memcpy(&actual_bytes, data + cursor, sizeof(U32));
    magnitude = Data::ensure_endian<stream_endian, native_endian>(actual_bytes);
    break;
  }

  case 8:
    memcpy(&magnitude, data + cursor, sizeof(U64));
    magnitude = Data::ensure_endian<stream_endian, native_endian>(magnitude);
    break;
  default: {
    Diagnostics::Message<128> error_message(Diagnostics::Log::Level::Error);
    error_message << "Serial read found invalid encoding size "_view
                  << (encoded_size) << " at byte location "_view << cursor
                  << "."_view;

    cursor = Count(-1);
    return Value();
  }
  }

  cursor += encoded_size;

  // Reconstruct the original two's complement bits with unsigned subtraction.
  // This restores the minimum S64 without signed overflow.
  U64 signed_bits = magnitude;
  if (byte_flag & negate_flag) {
    signed_bits = U64(0) - magnitude;
  }

  S64 value;
  Data::copy(Data::cast<U8>(&value), signed_bits);

  if (!(byte_flag & blob_flag)) {
    return Value(value);
  }

  if (cursor + value > source.get_size()) {
    Diagnostics::Message<128> error_message(Diagnostics::Log::Level::Error);
    error_message << "Serial read of blob sized "_view << value
                  << " bytes overran source buffer at location "_view << cursor
                  << ". source_size="_view << source.get_size()
                  << " blob_size="_view << value;

    cursor = Count(-1);
    return Value();
  }

  // The returned View references bytes in the Serial source. Advance past that
  // range so the next read begins at the following field.
  auto blob_start = data + cursor;
  cursor += value;
  if (value == 0) {
    return View::Bytes();
  }

  return View::Bytes(blob_start[0], value);
}

auto Reader::Serial::read_value() -> S64 {
  // Attribute failures from the shared decoder to the typed operation the
  // caller invoked rather than its internal `read` helper.
  auto attribution = Diagnostics::Log::set_attribution();
  return read().get_value();
}

auto Reader::Serial::read_blob() -> View::Bytes {
  // Attribute failures from the shared decoder to the typed operation the
  // caller invoked rather than its internal `read` helper.
  auto attribution = Diagnostics::Log::set_attribution();
  return read().get_view();
}
