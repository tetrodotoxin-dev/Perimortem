// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/library.hpp"

#ifdef PERI_WINDOWS
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "perimortem/core/static/bytes.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/core/writer/textual.hpp"

#include "perimortem/memory/dynamic/bytes.hpp"
#include "perimortem/memory/dynamic/vector.hpp"

using namespace Perimortem;

#ifdef PERI_WINDOWS
static auto library_error(Memory::Allocator::Arena& errors)
    -> Core::View::Bytes {
  const auto code = GetLastError();
  Core::Static::Bytes<96> buffer;
  Core::Writer::Textual message(buffer);
  message << "Windows library operation failed. error="_view << U32(code);
  return errors.proxy(message);
}
#endif

System::Library::Library(Library&& source) : handle(source.handle) {
  source.handle = nullptr;
}

System::Library::~Library() {
#ifdef PERI_WINDOWS
  if (handle && !FreeLibrary(static_cast<HMODULE>(handle))) {
    Core::Diagnostics::Log::fatal("Unable to release a Windows library."_view);
  }
#else
  if (handle && dlclose(handle) != 0) {
    Core::Diagnostics::Log::fatal(Core::NullTerminated::to_view(dlerror()));
  }
#endif
}

auto System::Library::open(
    Core::View::Bytes path,
    Memory::Allocator::Arena& errors)
    -> Utility::Result<Library, Core::View::Bytes> {
  Memory::Dynamic::Bytes name(path);
  name.append(0);
#ifdef PERI_WINDOWS
  const char* utf8 = reinterpret_cast<const char*>(name.get_view().get_data());
  const int size =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, nullptr, 0);
  if (!size) {
    return library_error(errors);
  }

  Memory::Dynamic::Vector<wchar_t> wide;
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
    return errors.proxy(Core::NullTerminated::to_view(dlerror()));
  }

#endif
  return Library(handle);
}

auto System::Library::symbol(
    Core::View::Bytes name,
    Memory::Allocator::Arena& errors) const
    -> Utility::Result<void*, Core::View::Bytes> {
  Memory::Dynamic::Bytes terminated(name);
  terminated.append(0);
#ifdef PERI_WINDOWS
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
    return errors.proxy(Core::NullTerminated::to_view(error));
  }

#endif
  return address;
}
