// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/filesystem/path.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/storage/bytes.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::System;
using namespace Toolchain::Validation;

using Path = Filesystem::Path;

static_assert(__is_trivially_copyable(Path));
static_assert(!__is_constructible(Path));
static_assert(!__is_constructible(Path::Native, const Path::Native&));

constexpr Path literal_path = "[work]:/source/main.ttx"_path;
static_assert(literal_path.get_anchor() == Path::Anchor::Work);
static_assert(literal_path.get_components() == "source/main.ttx"_view);
static_assert(literal_path.get_name() == "main.ttx"_view);
static_assert(literal_path.get_extension() == "ttx"_view);
static_assert(Path::create(Path::Anchor::Work, "source/main.ttx"_view));
static_assert(!Path::create(Path::Anchor::Work, "source/../main.ttx"_view));

static Harness SystemPath = {
  .name = "System::Filesystem::Path",
};

VALIDATION_TEST(SystemPath, anchors) {
  constexpr Path root = "[root]:/"_path;
  constexpr Path temp = "[temp]:/cache"_path;
  constexpr Path home = "[home]:/settings"_path;
  constexpr Path work = "[work]:/source"_path;
  constexpr Path exec = "[exec]:/resources"_path;

  EXPECT(root.get_anchor() == Path::Anchor::Root);
  EXPECT(temp.get_anchor() == Path::Anchor::Temp);
  EXPECT(home.get_anchor() == Path::Anchor::Home);
  EXPECT(work.get_anchor() == Path::Anchor::Work);
  EXPECT(exec.get_anchor() == Path::Anchor::Exec);
  EXPECT(root.get_components().is_empty());
}

VALIDATION_TEST(SystemPath, create) {
  auto selected = Path::create(Path::Anchor::Temp, "folder/file"_view);
  selected.visit(
      [&](const Path& path) {
        EXPECT(path.get_anchor() == Path::Anchor::Temp);
        EXPECT_EQ(path.get_components(), "folder/file"_view);
      },

      [&] { EXPECT(false); });

  const auto invalid_anchor = Path::Anchor(U8(-1));
  EXPECT_NOT(Path::create(invalid_anchor, "file"_view));
}

VALIDATION_TEST(SystemPath, parse_and_serialize) {
  auto parsed = Path::parse("[home]:/project/data.bin"_view);
  parsed.visit(
      [&](const Path& path) {
        Storage::Bytes<64> output;
        auto serialized = path.serialize(output);
        serialized.visit(
            [&](View::Bytes text) {
              EXPECT_EQ(text, "[home]:/project/data.bin"_view);
            },

            [&] { EXPECT(false); });
      },

      [&](Path::Failure) { EXPECT(false); });
}

VALIDATION_TEST(SystemPath, serialization_capacity) {
  constexpr Path path = "[exec]:/data"_path;
  Storage::Bytes<9> output;
  output = "unchanged"_view;

  EXPECT_NOT(path.serialize(output));
  EXPECT_EQ(output, "unchanged"_view);
}

VALIDATION_TEST(SystemPath, invalid_anchor) {
  auto missing = Path::parse("work/source"_view);
  auto unsupported = Path::parse("[data]:/source"_view);

  const auto expects_invalid_anchor = [&](const auto& result_value) {
    return result_value.visit(
        [](const Path&) { return False; },
        [](Path::Failure failure) {
          return Bool(failure == Path::Failure::InvalidAnchor);
        });
  };

  EXPECT(expects_invalid_anchor(missing));
  EXPECT(expects_invalid_anchor(unsupported));
}

VALIDATION_TEST(SystemPath, invalid_components) {
  constexpr View::Bytes invalid[] = {
    "/file"_view,
    "file/"_view,
    "folder//file"_view,
    "."_view,
    ".."_view,
    "folder/./file"_view,
    "folder/../file"_view,
    "folder\\file"_view,
  };

  for (View::Bytes components : invalid) {
    EXPECT_NOT(Path::create(Path::Anchor::Work, components));
  }

  U8 embedded_null[] = {'f', 'i', 'l', 'e', 0, 'x'};
  EXPECT_NOT(
      Path::create(
          Path::Anchor::Work,
          View::Bytes(embedded_null[0], sizeof(embedded_null))));
}

VALIDATION_TEST(SystemPath, utf8) {
  const U8 valid_text[] = {'c', 'a', 'f', 0xC3, 0xA9};
  const U8 overlong[] = {0xC0, 0xAF};
  const U8 surrogate[] = {0xED, 0xA0, 0x80};
  const U8 truncated[] = {0xF0, 0x9F, 0x92};

  EXPECT(
      Path::create(
          Path::Anchor::Home, View::Bytes(valid_text[0], sizeof(valid_text))));
  EXPECT_NOT(
      Path::create(
          Path::Anchor::Home, View::Bytes(overlong[0], sizeof(overlong))));
  EXPECT_NOT(
      Path::create(
          Path::Anchor::Home, View::Bytes(surrogate[0], sizeof(surrogate))));
  EXPECT_NOT(
      Path::create(
          Path::Anchor::Home, View::Bytes(truncated[0], sizeof(truncated))));

  auto parsed = Path::parse(View::Bytes(overlong[0], sizeof(overlong)));
  parsed.visit(
      [&](const Path&) { EXPECT(false); },
      [&](Path::Failure failure) {
        EXPECT(failure == Path::Failure::InvalidAnchor);
      });

  U8 canonical[] = {'[', 'w', 'o', 'r', 'k', ']', ':', '/', 0xC0, 0xAF};
  auto invalid = Path::parse(View::Bytes(canonical[0], sizeof(canonical)));
  invalid.visit(
      [&](const Path&) { EXPECT(false); },
      [&](Path::Failure failure) {
        EXPECT(failure == Path::Failure::InvalidEncoding);
      });
}

VALIDATION_TEST(SystemPath, parent_and_name) {
  constexpr Path path = "[root]:/unit/source/main.ttx"_path;
  EXPECT_EQ(path.get_name(), "main.ttx"_view);
  EXPECT_EQ(path.get_extension(), "ttx"_view);

  auto parent = path.try_parent();
  parent.visit(
      [&](const Path& selected) {
        EXPECT(selected.get_anchor() == Path::Anchor::Root);
        EXPECT_EQ(selected.get_components(), "unit/source"_view);
      },

      [&] { EXPECT(false); });

  constexpr Path hidden = "[root]:/unit/.main"_path;
  EXPECT(hidden.get_extension().is_empty());

  constexpr Path compound = "[root]:/unit/archive.tar.gz"_path;
  EXPECT_EQ(compound.get_extension(), "gz"_view);

  constexpr Path trailing = "[root]:/unit/main."_path;
  EXPECT(trailing.get_extension().is_empty());

  constexpr Path plain = "[root]:/unit/main"_path;
  EXPECT(plain.get_extension().is_empty());

  constexpr Path root = "[root]:/"_path;
  EXPECT_NOT(root.try_parent());
}

// Canonical Path validation is platform independent. Native emission resolves
// host anchors and therefore runs only where Perimortem supplies a native
// filesystem implementation.
#if defined(TOOLCHAIN_LINUX) || defined(TOOLCHAIN_WINDOWS)

VALIDATION_TEST(SystemPath, native_emission) {
  constexpr Path path = "[work]:/validation"_path;
  auto emitted = path.emit();
  emitted.visit(
      [&](const Path::Native& native) {
        ASSERT(native.get_data());
        EXPECT(native.get_data()[native.get_size()] == Path::NativeUnit());

#ifdef TOOLCHAIN_LINUX

        EXPECT(native.get_size() > 0);
        EXPECT(native.get_data()[0] == '/');

#elifdef TOOLCHAIN_WINDOWS

        ASSERT(native.get_size() >= 4);
        EXPECT(native.get_data()[0] == L'\\');
        EXPECT(native.get_data()[1] == L'\\');
        EXPECT(native.get_data()[2] == L'?');
        EXPECT(native.get_data()[3] == L'\\');

#endif
      },

      [&](Path::EmissionFailure) { EXPECT(false); });
}

VALIDATION_TEST(SystemPath, native_anchors) {
  constexpr Path::Anchor anchors[] = {
    Path::Anchor::Root, Path::Anchor::Temp, Path::Anchor::Home,
    Path::Anchor::Work, Path::Anchor::Exec,
  };

  for (Path::Anchor anchor : anchors) {
    auto path = Path::create(anchor, {});
    path.visit(
        [&](const Path& selected) {
          auto emitted = selected.emit();
          emitted.visit(
              [&](const Path::Native& native) {
                EXPECT(native.get_size() > 0);
                EXPECT(
                    native.get_data()[native.get_size()] == Path::NativeUnit());
              },

              [&](Path::EmissionFailure) { EXPECT(false); });
        },

        [&] { EXPECT(false); });
  }
}

VALIDATION_TEST(SystemPath, unbounded_native_emission) {
  Storage::Bytes<1024> components;
  for (Count index = 0; index < components.get_size(); index++) {
    components[index] = 'a';
  }

  auto path = Path::create(Path::Anchor::Temp, components);
  path.visit(
      [&](const Path& selected) {
        auto emitted = selected.emit();
        emitted.visit(
            [&](const Path::Native& native) {
              EXPECT(native.get_size() > components.get_size());
              EXPECT(
                  native.get_data()[native.get_size()] == Path::NativeUnit());
            },

            [&](Path::EmissionFailure) { EXPECT(false); });
      },

      [&] { EXPECT(false); });
}

#endif
