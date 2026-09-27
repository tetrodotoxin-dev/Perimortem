// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "tests/data.hpp"
#include "tests/process_support/fixture.hpp"

#include <stdlib.h>

#ifdef PERI_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "perimortem/core/static/vector.hpp"
#include "perimortem/core/null_terminated.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::Tests;

auto Perimortem::Tests::fixture_path() -> Dynamic::Bytes {
  if (getenv("TEST_WORKSPACE")) {
#ifdef PERI_WINDOWS
    return data_path("tests/process_fixture.exe"_view);
#else
    return data_path("tests/process_fixture"_view);
#endif
  }

  Dynamic::Bytes path;
#ifdef PERI_WINDOWS
  Static::Vector<wchar_t, 32768> wide;
  const DWORD size =
      GetModuleFileNameW(nullptr, wide.get_data(), wide.get_size());
  if (!size || size == wide.get_size()) {
    return path;
  }
  const int bytes = WideCharToMultiByte(
      CP_UTF8, 0, wide.get_data(), size, nullptr, 0, nullptr, nullptr);
  if (!bytes) {
    return path;
  }
  path.resize(bytes);
  WideCharToMultiByte(
      CP_UTF8, 0, wide.get_data(), size,
      reinterpret_cast<char*>(path.get_access().get_data()), bytes, nullptr,
      nullptr);
#else
  path.resize(4096);
  const auto size = readlink(
      "/proc/self/exe", reinterpret_cast<char*>(path.get_access().get_data()),
      path.get_size());
  if (size <= 0 || Count(size) == path.get_size()) {
    return Dynamic::Bytes();
  }
  path.resize(size);
#endif
  Count end = path.get_size();
  while (end && path[end - 1] != '/' && path[end - 1] != '\\') {
    --end;
  }
  path.resize(end);
#ifdef PERI_WINDOWS
  path.concat("process_fixture.exe"_view);
#else
  path.concat("process_fixture"_view);
#endif
  return path;
}
