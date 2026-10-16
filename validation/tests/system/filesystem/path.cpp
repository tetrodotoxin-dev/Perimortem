// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/filesystem/path.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/memory/thread/bytes.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;
using namespace Toolchain::Validation;

static Harness SystemPath = {
  .name = "System::Filesystem::Path",
};

VALIDATION_TEST(SystemPath, normalize) {
  Filesystem::Path path("unit\\./folder//file.ttx"_view);

  EXPECT_NOT(path.get_view().is_empty());
  EXPECT_TEXT(path.get_view(), "unit/folder/file.ttx"_view);
  EXPECT_NOT(path.is_rooted());
}

VALIDATION_TEST(SystemPath, normalize_in_arena) {
  Allocator::Arena arena;
  Thread::Bytes authored("unit\\./folder//file.ttx"_view);
  auto normalized = Filesystem::Path::normalize(arena, authored);
  auto rooted = Filesystem::Path::normalize(arena, "/usr//local/./bin"_view);
  ASSERT(normalized);
  ASSERT(rooted);

  // Mutating the source distinguishes the Arena copy from a View that merely
  // happened to contain the expected bytes during the call.
  authored.set('x');

  normalized.visit(
      [&](View::Bytes path) { EXPECT_TEXT(path, "unit/folder/file.ttx"_view); },
      []() {});
  rooted.visit(
      [&](View::Bytes path) { EXPECT_TEXT(path, "/usr/local/bin"_view); },
      []() {});
}

VALIDATION_TEST(SystemPath, normalize_invalid) {
  Allocator::Arena arena;
  Storage::Bytes<3> embedded_nul = {{'a', '\0', 'b'}};
  Storage::Bytes<Filesystem::Path::max_size + 1> oversized;
  for (Count i = 0; i < oversized.get_size(); i++) {
    oversized[i] = 'a';
  }

  // The oversized case uses one segment so failure proves the capacity bound
  // rather than an unrelated parent or separator rule.
  EXPECT_NOT(Filesystem::Path::normalize(arena, View::Bytes()));
  EXPECT_NOT(Filesystem::Path::normalize(arena, "."_view));
  EXPECT_NOT(Filesystem::Path::normalize(arena, "../file.ttx"_view));
  EXPECT_NOT(Filesystem::Path::normalize(arena, embedded_nul));
  EXPECT_NOT(Filesystem::Path::normalize(arena, oversized));
}

VALIDATION_TEST(SystemPath, relative) {
  Filesystem::Path path("unit/source/main.ttx"_view, "../shared/a.ttx"_view);

  EXPECT_NOT(path.get_view().is_empty());
  EXPECT_TEXT(path.get_view(), "unit/shared/a.ttx"_view);
}

VALIDATION_TEST(SystemPath, rooted) {
  Filesystem::Path path("/usr/local/bin"_view);

  EXPECT_NOT(path.get_view().is_empty());
  EXPECT_TEXT(path.get_view(), "/usr/local/bin"_view);
  EXPECT(path.is_rooted());
}

VALIDATION_TEST(SystemPath, escape) {
  Filesystem::Path path("../file.ttx"_view);
  Filesystem::Path relative_path("unit/main.ttx"_view, "../../file.ttx"_view);

  EXPECT(path.get_view().is_empty());
  EXPECT(relative_path.get_view().is_empty());
}

VALIDATION_TEST(SystemPath, root_parent) {
  Filesystem::Path path("/usr/.."_view);

  EXPECT_NOT(path.get_view().is_empty());
  EXPECT_TEXT(path.get_view(), "/"_view);
  EXPECT(path.is_rooted());
}

VALIDATION_TEST(SystemPath, file) {
  Filesystem::Path path("unit/source/main.ttx"_view);

  EXPECT_TEXT(path.get_file(), "main.ttx"_view);
}

VALIDATION_TEST(SystemPath, directory) {
  Filesystem::Path path("unit/source/main.ttx"_view);
  Filesystem::Path root_child("/main.ttx"_view);

  EXPECT_TEXT(path.get_directory(), "unit/source"_view);
  EXPECT_TEXT(root_child.get_directory(), "/"_view);
}

VALIDATION_TEST(SystemPath, extension) {
  Filesystem::Path path("unit/source/main.ttx"_view);
  Filesystem::Path no_extension("unit/source/main"_view);
  Filesystem::Path hidden("unit/source/.main"_view);

  EXPECT_TEXT(path.get_extension(), ".ttx"_view);
  EXPECT(no_extension.get_extension().is_empty());
  EXPECT(hidden.get_extension().is_empty());
}
