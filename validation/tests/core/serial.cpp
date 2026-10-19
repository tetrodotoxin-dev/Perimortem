// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/reader/serial.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/core/storage/vector.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/core/writer/serial.hpp"
#include "validation/tests/log.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Tests;
using namespace Toolchain::Validation;

static Harness CoreSerialReader = {
  .name = "Core::Reader::Serial",
  .setup = Log::begin,
  .teardown = Log::end,
};

VALIDATION_TEST(CoreSerialReader, regular_values) {
  Storage::Bytes<19> buffer =
      "\x01\xAB"
      "\x02\x34\x12"
      "\x04"
      "IREP"  // Stream is always little endian
      "\x08\xEF\xCD\xAB\x89\x67\x45\x23\x01"_view;
  Reader::Serial reader(buffer);

  EXPECT_EQ(reader.read_value(), 0xAB);
  EXPECT_EQ(reader.read_value(), 0x1234);
  EXPECT_EQ(reader.read_value(), 'PERI');
  EXPECT_EQ(reader.read_value(), S64(0x0123456789ABCDEF));
  EXPECT_EQ(reader.get_location(), reader.get_size());
}

VALIDATION_TEST(CoreSerialReader, negative_values) {
  Storage::Bytes<21> buffer =
      "\x11\xAB"
      "\x12\x34\x12"
      "\x14"
      "IREP"  // Stream is always little endian
      "\x18\xEF\xCD\xAB\x89\x67\x45\x23\x01"
      "\x11\x64"_view;
  Reader::Serial reader(buffer);

  EXPECT_EQ(reader.read_value(), -0xAB);
  EXPECT_EQ(reader.read_value(), -0x1234);
  EXPECT_EQ(reader.read_value(), -'PERI');
  EXPECT_EQ(reader.read_value(), S64(-0x0123456789ABCDEF));
  EXPECT_EQ(reader.read_value(), 0xFFFFFFFFFFFFFF9C);
  EXPECT_EQ(reader.get_location(), reader.get_size());
}

VALIDATION_TEST(CoreSerialReader, negative_boundaries) {
  constexpr S64 minimum = -9223372036854775807LL - 1;
  Storage::Bytes<11> buffer =
      "\x11\x01"
      "\x18\x00\x00\x00\x00\x00\x00\x00\x80"_view;
  Reader::Serial reader(buffer);

  EXPECT_EQ(reader.read_value(), -1);
  EXPECT_EQ(reader.read_value(), minimum);
  EXPECT_EQ(reader.get_location(), reader.get_size());
}

VALIDATION_TEST(CoreSerialReader, small_blobs) {
  Storage::Bytes<30> buffer =
      "\x21\x0A"
      "Perimortem"
      "\x01\x04"
      "\x21\x0E"
      "Testing String"_view;
  Reader::Serial reader(buffer);

  EXPECT_EQ(reader.read_blob(), "Perimortem"_view);
  EXPECT_EQ(reader.read_value(), 4);
  EXPECT_EQ(reader.read_blob(), "Testing String"_view);
  EXPECT_EQ(reader.get_location(), reader.get_size());
}

VALIDATION_TEST(CoreSerialReader, large_blob) {
  Storage::Bytes<500> expected([](Count i) -> U8 { return i; });
  Storage::Bytes<503> source([](Count i) -> U8 {
    // Header
    if (i < 3) {
      return "\x22\xF4\x01"_view[i];
    }

    return (i - 3);
  });

  Reader::Serial reader(source);

  EXPECT_HEX(reader.read_blob(), expected.get_view());
  EXPECT_EQ(reader.get_location(), reader.get_size());
}

VALIDATION_TEST(CoreSerialReader, only_values) {
  Storage::Bytes<30> buffer =
      "\x21\x0A"
      "Perimortem"
      "\x01\x04"
      "\x21\x0E"
      "Testing String"_view;
  Reader::Serial reader(buffer);

  EXPECT_EQ(reader.read_value(), 0);
  EXPECT_EQ(reader.read_value(), 4);
  EXPECT_EQ(reader.read_value(), 0);
  EXPECT_EQ(reader.get_location(), reader.get_size());
}

VALIDATION_TEST(CoreSerialReader, only_blobs) {
  Storage::Bytes<30> buffer =
      "\x21\x0A"
      "Perimortem"
      "\x01\x04"
      "\x21\x0E"
      "Testing String"_view;
  Reader::Serial reader(buffer);

  EXPECT_EQ(reader.read_blob(), "Perimortem"_view);
  EXPECT_EQ(reader.read_blob(), ""_view);
  EXPECT_EQ(reader.read_blob(), "Testing String"_view);
  EXPECT_EQ(reader.get_location(), reader.get_size());
}

VALIDATION_TEST(CoreSerialReader, type_mismatch) {
  Reader::Serial reader("\x18\xAD\xDE\x00\x00"_view);
  auto scope_attribution = Diagnostics::Log::set_attribution();

  EXPECT_EQ(reader.read_value(), 0);
  EXPECT_EQ(reader.get_location(), Count(-1));

  constexpr auto error_message =
      "Serial read overran data buffer while reading value at byte location 1. source_size=5 encoded_size=8"_view;
  EXPECT(Log::error_contains(error_message));
}

VALIDATION_TEST(CoreSerialReader, bad_encoding) {
  Reader::Serial reader("\x13\xAD\xDE\x00\x00"_view);
  auto scope_attribution = Diagnostics::Log::set_attribution();

  EXPECT_EQ(reader.read_value(), 0);
  EXPECT_EQ(reader.get_location(), Count(-1));

  constexpr auto error_message =
      "Serial read found invalid encoding size 3 at byte location 1."_view;
  EXPECT(Log::error_contains(error_message));
}

VALIDATION_TEST(CoreSerialReader, bad_blob) {
  Reader::Serial reader("\x24\xAD\xDE\x00\x00"_view);
  auto scope_attribution = Diagnostics::Log::set_attribution();

  EXPECT_EQ(reader.read_value(), 0);
  EXPECT_EQ(reader.get_location(), Count(-1));

  constexpr auto error_message =
      "Serial read of blob sized 57005 bytes overran source buffer at location "
      "5. source_size=5 blob_size=57005"_view;
  EXPECT(Log::error_contains(error_message));
}

VALIDATION_TEST(CoreSerialReader, negative_blob) {
  Reader::Serial reader("\x31\x01"_view);
  auto scope_attribution = Diagnostics::Log::set_attribution();

  EXPECT_EQ(reader.read_blob(), View::Bytes());
  EXPECT_EQ(reader.get_location(), Count(-1));

  constexpr auto error_message =
      "Serial read found a negative blob size at byte location 0."_view;
  EXPECT(Log::error_contains(error_message));
}

VALIDATION_TEST(CoreSerialReader, read_from_empty) {
  Reader::Serial reader(""_view);
  auto scope_attribution = Diagnostics::Log::set_attribution();

  EXPECT_EQ(reader.read_value(), 0);
  EXPECT_EQ(reader.get_location(), Count(-1));

  constexpr auto error_message =
      "Serial read overran data buffer while reading type at byte location 0."_view;
  EXPECT(Log::error_contains(error_message));
}

static Harness CoreSerialWriter = {
  .name = "Core::Writer::Serial",
  .setup = Log::begin,
  .teardown = Log::end,
};

VALIDATION_TEST(CoreSerialWriter, regular_values) {
  Storage::Bytes<19> buffer;
  Writer::Serial writer(buffer);

  writer << 0xAB << 0x1234 << 'PERI' << 0x0123456789ABCDEF;

  EXPECT(writer.is_valid());
  EXPECT_EQ(writer.get_location(), 19);
  EXPECT_HEX(
      buffer,
      "\x01\xAB"
      "\x02\x34\x12"
      "\x04"
      "IREP"  // Stream is always little endian
      "\x08\xEF\xCD\xAB\x89\x67\x45\x23\x01"_view);
}

VALIDATION_TEST(CoreSerialWriter, negative_values) {
  Storage::Bytes<21> buffer;
  Writer::Serial writer(buffer);

  // Exercise every payload width with negative magnitudes. The expected type
  // bytes also prove that each value carries the sign flag.
  writer << -0xAB << -0x1234 << -'PERI' << -0x0123456789ABCDEF;

  writer << S64(-100);

  EXPECT(writer.is_valid());
  EXPECT_HEX(
      buffer,
      "\x11\xAB"
      "\x12\x34\x12"
      "\x14"
      "IREP"  // Stream is always little endian
      "\x18\xEF\xCD\xAB\x89\x67\x45\x23\x01"
      "\x11\x64"_view);
}

VALIDATION_TEST(CoreSerialWriter, negative_boundaries) {
  constexpr S64 minimum = -9223372036854775807LL - 1;
  constexpr S64 maximum = 9223372036854775807LL;
  Storage::Bytes<20> buffer;
  Writer::Serial writer(buffer);

  writer << -1 << minimum << maximum;

  EXPECT(writer.is_valid());
  EXPECT_HEX(
      buffer,
      "\x11\x01"
      "\x18\x00\x00\x00\x00\x00\x00\x00\x80"
      "\x08\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x7F"_view);
}

VALIDATION_TEST(CoreSerialWriter, small_blobs) {
  Storage::Bytes<30> buffer;
  Writer::Serial writer(buffer);

  writer << "Perimortem"_view << 4 << "Testing String"_view;

  EXPECT(writer.is_valid());
  EXPECT_HEX(
      buffer,
      "\x21\x0A"
      "Perimortem"
      "\x01\x04"
      "\x21\x0E"
      "Testing String"_view);
}

VALIDATION_TEST(CoreSerialWriter, large_blob) {
  Storage::Bytes<500> source([](Count i) -> U8 { return i; });
  Storage::Bytes<503> expected([](Count i) -> U8 {
    // Header
    if (i < 3) {
      return "\x22\xF4\x01"_view[i];
    }

    return (i - 3);
  });

  Storage::Bytes<expected.get_size()> buffer;
  Writer::Serial writer(buffer);

  writer << source;

  EXPECT(writer.is_valid());
  EXPECT_HEX(buffer, expected);
}

VALIDATION_TEST(CoreSerialWriter, overflow) {
  Storage::Bytes<20> buffer;
  for (Count index = 0; index < buffer.get_size(); index++) {
    buffer[index] = '?';
  }

  Writer::Serial writer(buffer);

  writer << "Perimortem"_view << 4 << "Testing String"_view;

  EXPECT_NOT(writer.is_valid());
  EXPECT_EQ(writer.get_location(), Count(14));
  EXPECT_TEXT(buffer.slice(14, 6), "??????"_view);

  writer << S64(1);
  EXPECT_EQ(writer.get_location(), Count(14));
  EXPECT_TEXT(buffer.slice(14, 6), "??????"_view);
}

VALIDATION_TEST(CoreSerialWriter, blob_preflight) {
  Storage::Bytes<3> buffer;
  for (Count index = 0; index < buffer.get_size(); index++) {
    buffer[index] = '?';
  }

  Writer::Serial writer(buffer);
  writer << "ab"_view;

  EXPECT_NOT(writer.is_valid());
  EXPECT_EQ(writer.get_location(), Count(0));
  EXPECT_TEXT(buffer, "???"_view);
}

VALIDATION_TEST(CoreSerialWriter, pointer_bounds) {
  Storage::Bytes<4> buffer;
  Writer::Serial writer(buffer);

  writer.set_pointer(Count(-1));
  writer << U64(1);

  EXPECT_EQ(writer.get_location(), writer.get_size());
  EXPECT_NOT(writer.is_valid());
}

VALIDATION_TEST(CoreSerialWriter, copy_state) {
  Storage::Bytes<8> buffer;
  for (Count index = 0; index < buffer.get_size(); index++) {
    buffer[index] = '?';
  }

  Writer::Serial original(buffer);
  original << S64(1);

  Writer::Serial copy(original);

  original << S64(2);
  copy << S64(3);
  EXPECT_EQ(original.get_location(), Count(4));
  EXPECT_EQ(copy.get_location(), Count(4));
  EXPECT_HEX(buffer, "\x01\x01\x01\x03????"_view);

  original << "12345"_view;

  Writer::Serial invalid_copy(original);
  EXPECT_NOT(original.is_valid());
  EXPECT_NOT(invalid_copy.is_valid());
  EXPECT_EQ(invalid_copy.get_location(), Count(4));

  invalid_copy << S64(4);
  EXPECT_EQ(invalid_copy.get_location(), Count(4));
  EXPECT_HEX(buffer, "\x01\x01\x01\x03????"_view);
}
