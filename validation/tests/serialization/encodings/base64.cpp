// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/serialization/encodings/base64.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/access/vector.hpp"
#include "perimortem/core/data.hpp"
#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/core/view/vector.hpp"
#include "perimortem/system/filesystem/file.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::Serialization;
using namespace Perimortem::System;

using namespace Toolchain::Validation;

static Harness SerializationBase64 = {
  .name = "Serialization::Encodings::Base64",
};

static auto decode(View::Bytes source, Test::TestResult& result)
    -> Thread::Bytes {
  auto decoded = Encodings::Base64::decode(source);
  return decoded.visit(
      [](Thread::Bytes& bytes) { return Data::take(bytes); },

      [&] {
        EXPECT(false);
        return Thread::Bytes();
      });
}

static auto decode(
    Allocator::Arena& arena,
    View::Bytes source,
    Test::TestResult& result) -> View::Bytes {
  auto decoded = Encodings::Base64::decode(arena, source);
  return decoded.visit(
      [](View::Bytes bytes) { return bytes; },

      [&] {
        EXPECT(false);
        return View::Bytes();
      });
}

static auto expect_decode_failure(View::Bytes source, Test::TestResult& result)
    -> void {
  auto decoded = Encodings::Base64::decode(source);
  decoded.visit(
      [&](Thread::Bytes&) { EXPECT(false); },

      [] {});

  Allocator::Arena arena;
  auto decoded_view = Encodings::Base64::decode(arena, source);
  decoded_view.visit(
      [&](View::Bytes) { EXPECT(false); },

      [] {});
}

VALIDATION_TEST(SerializationBase64, decode_empty) {
  const auto source = ""_view;
  const auto decoded_bytes = decode(source, result);
  Allocator::Arena arena;
  const auto decoded_view = decode(arena, source, result);

  EXPECT_TEXT(decoded_bytes.get_view(), ""_view);
  EXPECT_TEXT(decoded_view, ""_view);
}

VALIDATION_TEST(SerializationBase64, reject_partial_quartet) {
  U8 source[] = {'A', 'A', 'A', 'A', 'A', 'A', 'A'};
  for (Count size = 1; size <= sizeof(source); size++) {
    if (size == 4) {
      continue;
    }

    View::Bytes encoded(source[0], size);
    expect_decode_failure(encoded, result);
  }
}

VALIDATION_TEST(SerializationBase64, reject_invalid_characters) {
  expect_decode_failure("AAA?"_view, result);
}

VALIDATION_TEST(SerializationBase64, reject_invalid_padding) {
  constexpr View::Bytes invalid[] = {
    "AA=A"_view,
    "A==="_view,
    "AB=="_view,
    "AAB="_view,
  };

  for (View::Bytes source : invalid) {
    expect_decode_failure(source, result);
  }
}

VALIDATION_TEST(SerializationBase64, decode_simple) {
  const auto source = "Base64 test string for Perimortem."_view;
  const auto encoded = "QmFzZTY0IHRlc3Qgc3RyaW5nIGZvciBQZXJpbW9ydGVtLg=="_view;
  const auto decoded_bytes = decode(encoded, result);

  EXPECT_TEXT(decoded_bytes.get_view(), source);
}

VALIDATION_TEST(SerializationBase64, alphabet) {
  constexpr auto alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"_view;
  Storage::Bytes<48> expected;
  for (Count input = 0, output = 0; input < alphabet.get_size();
       input += 4, output += 3) {
    expected[output] = U8((input << 2) | ((input + 1) >> 4));
    expected[output + 1] = U8(((input + 1) << 4) | ((input + 2) >> 2));
    expected[output + 2] = U8(((input + 2) << 6) | (input + 3));
  }

  const auto decoded = decode(alphabet, result);
  EXPECT(decoded.get_view() == expected.get_view());
  EXPECT(Encodings::Base64::encode(expected) == alphabet);
}

VALIDATION_TEST(SerializationBase64, padded_vector_tail) {
  // A final padded quartet can end exactly at a vector batch boundary. It
  // still belongs to the scalar tail, otherwise subtracting that batch from
  // the unpadded input count underflows and the decoder walks past the source.
  U8 encoded[192];
  for (Count size = 96; size <= 192; size += 96) {
    for (Count padding = 1; padding <= 2; ++padding) {
      for (auto& byte : encoded) {
        byte = 'A';
      }

      for (Count index = 0; index < padding; ++index) {
        encoded[size - index - 1] = '=';
      }

      const View::Bytes source(encoded[0], size);
      const auto decoded = decode(source, result);
      Allocator::Arena arena;
      const auto decoded_view = decode(arena, source, result);
      EXPECT(decoded.get_size() == size / 4 * 3 - padding);
      EXPECT(decoded_view == decoded.get_view());
      for (Count index = 0; index < decoded.get_size(); ++index) {
        EXPECT(decoded.get_view()[index] == 0);
      }
    }
  }
}

#if defined(TOOLCHAIN_LINUX) || defined(TOOLCHAIN_WINDOWS)

VALIDATION_TEST(SerializationBase64, decode_image) {
  auto source = Filesystem::File::read(
      "[work]:/validation/data/pngs/perimortem_icon.png"_path);
  auto base64 = Filesystem::File::read(
      "[work]:/validation/data/base64/perimortem_icon.base64"_path);
  ASSERT(source);
  ASSERT(base64);
  source.visit(
      [&](Global::Bytes& source_bytes) {
        base64.visit(
            [&](Global::Bytes& base64_bytes) {
              ASSERT_NOT(source_bytes.is_empty());
              ASSERT_NOT(base64_bytes.is_empty());
              ASSERT(
                  base64_bytes.get_view()[base64_bytes.get_size() - 1] == '\n');

              const View::Bytes encoded =
                  base64_bytes.get_view().slice(0, base64_bytes.get_size() - 1);

              // This binary fixture is independent of the evolving TTX source
              // grammar. Regenerate the golden encoding only when intentionally
              // replacing the PNG.
              const auto decoded_bytes = decode(encoded, result);
              EXPECT(decoded_bytes.get_view() == source_bytes.get_view());
            },

            []() {});
      },

      []() {});
}

#endif

VALIDATION_TEST(SerializationBase64, encode_empty) {
  const auto source = ""_view;
  const auto decoded_bytes = Encodings::Base64::encode(source);

  EXPECT_TEXT(decoded_bytes.get_view(), ""_view);
}

VALIDATION_TEST(SerializationBase64, encode_simple) {
  const auto source = "Base64 test string for Perimortem."_view;
  const auto encoded = "QmFzZTY0IHRlc3Qgc3RyaW5nIGZvciBQZXJpbW9ydGVtLg=="_view;
  const auto encoded_bytes = Encodings::Base64::encode(source);

  EXPECT_TEXT(encoded_bytes.get_view(), encoded);
}

#if defined(TOOLCHAIN_LINUX) || defined(TOOLCHAIN_WINDOWS)

VALIDATION_TEST(SerializationBase64, encode_image) {
  auto source = Filesystem::File::read(
      "[work]:/validation/data/pngs/perimortem_icon.png"_path);
  auto base64 = Filesystem::File::read(
      "[work]:/validation/data/base64/perimortem_icon.base64"_path);
  ASSERT(source);
  ASSERT(base64);
  source.visit(
      [&](Global::Bytes& source_bytes) {
        base64.visit(
            [&](Global::Bytes& base64_bytes) {
              ASSERT_NOT(source_bytes.is_empty());
              ASSERT_NOT(base64_bytes.is_empty());
              ASSERT(
                  base64_bytes.get_view()[base64_bytes.get_size() - 1] == '\n');

              const View::Bytes encoded =
                  base64_bytes.get_view().slice(0, base64_bytes.get_size() - 1);

              // This binary fixture is independent of the evolving TTX source
              // grammar. Regenerate the golden encoding only when intentionally
              // replacing the PNG.
              const auto encoded_bytes =
                  Encodings::Base64::encode(source_bytes.get_view());
              EXPECT_TEXT(encoded_bytes.get_view(), encoded);
            },

            []() {});
      },

      []() {});
}

#endif
