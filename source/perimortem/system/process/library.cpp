// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/process/library.hpp"

#ifdef TOOLCHAIN_WINDOWS
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "perimortem/core/data.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/core/writer/textual.hpp"
#include "perimortem/memory/thread/bytes.hpp"
#include "perimortem/memory/thread/vector.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;

#ifdef TOOLCHAIN_WINDOWS

static auto library_error(Allocator::Arena& errors) -> View::Bytes {
  const auto code = GetLastError();
  Storage::Bytes<96> buffer;
  Writer::Textual message(buffer);
  message << "Windows library operation failed. error="_view << U32(code);
  return errors.copy(message);
}

#else

// Loader errors have no supplied extent. Cap the diagnostic prefix before
// copying it into the error arena.
constexpr Count max_loader_message_size = 4096;

#endif

Process::Library::Library(Library&& source) : handle(source.handle) {
  source.handle = nullptr;
}

Process::Library::~Library() {

  // Every Library use has ended before destruction requests unload. A host
  // rejection can retain native module resources, but it cannot leave a caller
  // executing through this Library, so report those resources without aborting.

#ifdef TOOLCHAIN_WINDOWS

  // `open` stores LoadLibraryExW's HMODULE in Library's `void*` field. Recreate
  // the Windows handle type only while returning that same value to
  // FreeLibrary.
  if (handle && !FreeLibrary(Data::bit_cast<HMODULE>(handle))) {
    const U32 unload_error = GetLastError();
    Diagnostics::Log::Message<256> failure(Diagnostics::Log::Level::Error);
    failure << "Process::Library could not release its native module handle. "
               "The module may remain loaded until process exit. error="_view
            << unload_error;
  }

#else

  if (handle && dlclose(handle) != 0) {
    Diagnostics::Log::Message<max_loader_message_size + 192> failure(
        Diagnostics::Log::Level::Error);
    failure
        << "Process::Library could not release its native module handle. "
           "The module may remain loaded until process exit. error="_view
        << View::NullTerminated(dlerror(), max_loader_message_size).get_view();
  }

#endif
}

auto Process::Library::open(View::Bytes path, Allocator::Arena& errors)
    -> Storage::Result<Library, View::Bytes> {
  Thread::Bytes name(path);
  name.append(0);

#ifdef TOOLCHAIN_WINDOWS

  const char* utf8 = Data::cast<const char>(name.get_view().get_data());
  const S32 size =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, nullptr, 0);
  if (!size) {
    return library_error(errors);
  }

  Thread::Vector<wchar_t> wide;
  // `MultiByteToWideChar` writes the complete requested range or reports
  // failure. The temporary buffer needs no initialized value before that call.
  wide.forgetful_resize(size);

  const S32 written = MultiByteToWideChar(
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
      Data::cast<const char>(name.get_view().get_data()),
      RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    return errors.copy(
        View::NullTerminated(dlerror(), max_loader_message_size).get_view());
  }

#endif

  return Library(handle);
}

auto Process::Library::symbol(View::Bytes name, Allocator::Arena& errors) const
    -> Storage::Result<void*, View::Bytes> {
  Thread::Bytes terminated(name);
  terminated.append(0);

#ifdef TOOLCHAIN_WINDOWS

  // Windows declares all exported addresses as FARPROC even when the export is
  // data. Convert that loader result only at the untyped Library boundary.
  auto* address = Data::bit_cast<void*>(GetProcAddress(
      Data::bit_cast<HMODULE>(handle),
      Data::cast<const char>(terminated.get_view().get_data())));
  if (!address) {
    return library_error(errors);
  }

#else

  dlerror();

  auto* address =
      dlsym(handle, Data::cast<const char>(terminated.get_view().get_data()));
  if (const auto* error = dlerror()) {
    return errors.copy(
        View::NullTerminated(error, max_loader_message_size).get_view());
  }

#endif

  return address;
}
