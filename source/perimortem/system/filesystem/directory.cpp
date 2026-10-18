// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/filesystem/directory.hpp"

#ifdef TOOLCHAIN_LINUX
#include <sys/stat.h>
#elifdef TOOLCHAIN_WINDOWS
#include <windows.h>
#endif

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;

static auto is_directory(const Filesystem::Path::Native& path) -> Bool {

#ifdef TOOLCHAIN_LINUX

  struct stat64 status;
  return Bool(stat64(path.get_data(), &status) == 0 && S_ISDIR(status.st_mode));

#elifdef TOOLCHAIN_WINDOWS

  const U32 attributes = GetFileAttributesW(path.get_data());
  return Bool(
      attributes != INVALID_FILE_ATTRIBUTES &&
      (attributes & FILE_ATTRIBUTE_DIRECTORY));

#else

  return False;

#endif
}

auto Filesystem::Directory::open(const Path& path)
    -> Storage::Option<Directory> {
  const auto select_directory =
      [&path](const Path::Native& native) -> Storage::Option<Directory> {
    if (!is_directory(native)) {
      return {};
    }

    return Directory(path.get_anchor(), Global::Bytes(path.get_components()));
  };

  const auto emission_failed =
      [](Path::EmissionFailure) -> Storage::Option<Directory> { return {}; };
  return path.emit().visit(select_directory, emission_failed);
}

auto Filesystem::Directory::current() -> Storage::Option<Directory> {
  constexpr Path working_directory = "[work]:/"_path;
  return open(working_directory);
}
