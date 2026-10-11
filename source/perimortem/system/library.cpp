// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/library.hpp"

#ifdef TOOLCHAIN_WINDOWS
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/core/static/bytes.hpp"
#include "perimortem/core/writer/textual.hpp"
#include "perimortem/memory/dynamic/bytes.hpp"
#include "perimortem/memory/dynamic/vector.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;
using namespace Perimortem::Utility;

#ifdef TOOLCHAIN_WINDOWS

static auto library_error(Allocator::Arena& errors) -> View::Bytes {
  const auto code = GetLastError();
  Static::Bytes<96> buffer;
  Writer::Textual message(buffer);
  message << "Windows library operation failed. error="_view << U32(code);
  return errors.proxy(message);
}

#else

// Loader errors have no supplied extent. Cap the diagnostic prefix before
// copying it into the error arena.
constexpr Count max_loader_message_size = 4096;

#endif

Library::Library(Library&& source) : handle(source.handle) {
  source.handle = nullptr;
}

Library::~Library() {

#ifdef TOOLCHAIN_WINDOWS

  if (handle && !FreeLibrary(static_cast<HMODULE>(handle))) {
    Diagnostics::Log::fatal("Unable to release a Windows library."_view);
  }

#else

  if (handle && dlclose(handle) != 0) {
    Diagnostics::Log::fatal(
        NullTerminated(dlerror(), max_loader_message_size).get_view());
  }

#endif
}

auto Library::open(View::Bytes path, Allocator::Arena& errors)
    -> Result<Library, View::Bytes> {
  Dynamic::Bytes name(path);
  name.append(0);

#ifdef TOOLCHAIN_WINDOWS

  const char* utf8 = reinterpret_cast<const char*>(name.get_view().get_data());
  const int size =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, nullptr, 0);
  if (!size) {
    return library_error(errors);
  }

  Dynamic::Vector<wchar_t> wide;
  wide.resize(size);

  const int written = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide.get_data(), size);
  if (!written) {
    return library_error(errors);
  }

  auto* handle = LoadLibraryExW(wide.get_data(), nullptr, 0);
  if (!handle) {
    return library_error(errors);
  }

#else

  auto* handle = dlopen(
      reinterpret_cast<const char*>(name.get_view().get_data()),
      RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    return errors.proxy(
        NullTerminated(dlerror(), max_loader_message_size).get_view());
  }

#endif

  return Library(handle);
}

auto Library::symbol(View::Bytes name, Allocator::Arena& errors) const
    -> Result<void*, View::Bytes> {
  Dynamic::Bytes terminated(name);
  terminated.append(0);

#ifdef TOOLCHAIN_WINDOWS

  auto* address = reinterpret_cast<void*>(GetProcAddress(
      static_cast<HMODULE>(handle),
      reinterpret_cast<const char*>(terminated.get_view().get_data())));
  if (!address) {
    return library_error(errors);
  }

#else

  dlerror();

  auto* address = dlsym(
      handle, reinterpret_cast<const char*>(terminated.get_view().get_data()));
  if (const auto* error = dlerror()) {
    return errors.proxy(
        NullTerminated(error, max_loader_message_size).get_view());
  }

#endif

  return address;
}
