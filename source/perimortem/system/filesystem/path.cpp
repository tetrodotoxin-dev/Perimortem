// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/filesystem/path.hpp"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef TOOLCHAIN_LINUX
#include <pwd.h>
#include <unistd.h>
#elifdef TOOLCHAIN_WINDOWS
#include <windows.h>
#endif

#include "perimortem/core/data.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;

using NativeUnits = Global::Vector<Filesystem::Path::NativeUnit>;

template <typename unit_type>
static auto copy_units(const unit_type* source, Count size)
    -> Global::Vector<unit_type> {
  Global::Vector<unit_type> result;
  result.resize(size);
  for (Count index = 0; index < size; index++) {
    result.get_data()[index] = source[index];
  }

  return result;
}

#ifdef TOOLCHAIN_LINUX

static auto capture_current_directory() -> NativeUnits {
  // `getcwd` reports ERANGE instead of a required size. Geometric retries avoid
  // imposing the former fixed path limit on the Work anchor.
  Count capacity = 256;
  while (capacity <= Count(CppSize(-1))) {
    NativeUnits result;
    result.resize(capacity);
    errno = 0;
    if (getcwd(result.get_data(), CppSize(capacity))) {
      result.retain(0, strlen(result.get_data()));
      return result;
    }

    if (errno != ERANGE || capacity > Count(CppSize(-1)) / 2) {
      return {};
    }

    capacity *= 2;
  }

  return {};
}

static auto capture_executable_directory() -> NativeUnits {
  // `/proc/self/exe` omits a terminator and can exactly fill the supplied
  // buffer. Only a shorter result proves the executable path is complete.
  Count capacity = 256;
  while (capacity <= Count(CppSize(-1))) {
    NativeUnits result;
    result.resize(capacity);

    const S64 written =
        readlink("/proc/self/exe", result.get_data(), CppSize(capacity));
    if (written < 0) {
      return {};
    }

    if (Count(written) < capacity) {
      Count directory_size = 0;
      for (Count index = 0; index < Count(written); index++) {
        if (result.get_data()[index] == '/') {
          directory_size = index == 0 ? Count(1) : index;
        }
      }

      if (directory_size == 0) {
        return {};
      }

      result.retain(0, directory_size);
      return result;
    }

    if (capacity > Count(CppSize(-1)) / 2) {
      return {};
    }

    capacity *= 2;
  }

  return {};
}

static auto capture_environment_directory(const char* name) -> NativeUnits {
  const char* value = getenv(name);
  if (!value || value[0] != '/') {
    return {};
  }

  return copy_units(value, strlen(value));
}

static auto capture_home_directory() -> NativeUnits {
  NativeUnits home = capture_environment_directory("HOME");
  if (!home.is_empty()) {
    return home;
  }

  // Services do not necessarily inherit HOME. The account database supplies
  // the same user location without making that environment variable mandatory.
  const S64 suggested_size = sysconf(_SC_GETPW_R_SIZE_MAX);
  Count capacity = suggested_size > 0 ? Count(suggested_size) : Count(4096);
  while (capacity <= Count(CppSize(-1))) {
    NativeUnits storage;
    storage.resize(capacity);

    struct passwd record;
    struct passwd* selected = nullptr;
    const S32 error = getpwuid_r(
        geteuid(), &record, storage.get_data(), CppSize(capacity), &selected);
    if (error == 0) {
      if (!selected || !record.pw_dir || record.pw_dir[0] != '/') {
        return {};
      }

      return copy_units(record.pw_dir, strlen(record.pw_dir));
    }

    if (error != ERANGE || capacity > Count(CppSize(-1)) / 2) {
      return {};
    }

    capacity *= 2;
  }

  return {};
}

#elifdef TOOLCHAIN_WINDOWS

template <typename reader_type>
static auto capture_resizable(reader_type reader) -> NativeUnits {
  // Windows directory APIs report the required unit count when the supplied
  // buffer is short. Retrying that exact size preserves paths beyond MAX_PATH.
  Count capacity = 256;
  while (capacity <= Count(U32(-1))) {
    NativeUnits result;
    result.resize(capacity);

    const U32 written = reader(U32(capacity), result.get_data());
    if (written == 0) {
      return {};
    }

    if (Count(written) < capacity) {
      result.retain(0, written);
      return result;
    }

    capacity = Count(written) + 1;
  }

  return {};
}

static auto capture_current_directory() -> NativeUnits {
  return capture_resizable([](U32 capacity, wchar_t* output) {
    return GetCurrentDirectoryW(capacity, output);
  });
}

static auto capture_temporary_directory() -> NativeUnits {
  return capture_resizable([](U32 capacity, wchar_t* output) {
    return GetTempPathW(capacity, output);
  });
}

static auto capture_executable_directory() -> NativeUnits {
  // GetModuleFileName reports a full buffer for truncation. Growing until the
  // returned count is shorter preserves the complete executable directory.
  Count capacity = 256;
  while (capacity <= Count(U32(-1))) {
    NativeUnits result;
    result.resize(capacity);
    SetLastError(ERROR_SUCCESS);

    const U32 written =
        GetModuleFileNameW(nullptr, result.get_data(), U32(capacity));
    if (written == 0) {
      return {};
    }

    if (Count(written) < capacity &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
      Count directory_size = 0;
      for (Count index = 0; index < written; index++) {
        if (result.get_data()[index] == L'\\' ||
            result.get_data()[index] == L'/') {
          directory_size = index;
        }
      }

      if (directory_size == 0) {
        return {};
      }

      result.retain(0, directory_size);
      return result;
    }

    if (capacity > Count(U32(-1)) / 2) {
      return {};
    }

    capacity *= 2;
  }

  return {};
}

static auto capture_environment_value(const wchar_t* name) -> NativeUnits {
  const U32 required = GetEnvironmentVariableW(name, nullptr, 0);
  if (required == 0) {
    return {};
  }

  NativeUnits result;
  result.resize(required);

  const U32 written =
      GetEnvironmentVariableW(name, result.get_data(), required);
  if (written == 0 || written >= required) {
    return {};
  }

  result.retain(0, written);
  return result;
}

static auto capture_home_directory() -> NativeUnits {
  NativeUnits home = capture_environment_value(L"USERPROFILE");
  if (!home.is_empty()) {
    return home;
  }

  // USERPROFILE normally supplies the complete location. When it is absent,
  // both HOMEDRIVE and HOMEPATH are required to reconstruct an absolute home
  // anchor. Either value alone is rejected.
  NativeUnits drive = capture_environment_value(L"HOMEDRIVE");
  NativeUnits path = capture_environment_value(L"HOMEPATH");
  if (drive.is_empty() || path.is_empty()) {
    return {};
  }

  drive.concat(path.get_view());
  return drive;
}

static auto capture_root(View::Vector<wchar_t> work) -> NativeUnits {
  // A Windows process has no single `/` root. Root follows the captured Work
  // volume or UNC share, while extended spellings retain their routing.
  const Bool extended = work.get_size() >= 4 && work[0] == L'\\' &&
                        work[1] == L'\\' && work[2] == L'?' && work[3] == L'\\';
  if (extended && work.get_size() >= 7 && work[5] == L':' &&
      (work[6] == L'\\' || work[6] == L'/')) {
    return copy_units(work.get_data(), 7);
  }

  if (work.get_size() >= 3 && work[1] == L':' &&
      (work[2] == L'\\' || work[2] == L'/')) {
    return copy_units(work.get_data(), 3);
  }

  const Bool extended_unc = extended && work.get_size() >= 8 &&
                            work[4] == L'U' && work[5] == L'N' &&
                            work[6] == L'C' && work[7] == L'\\';
  const Bool native_unc =
      !extended && work.get_size() >= 5 && work[0] == L'\\' && work[1] == L'\\';
  if (extended_unc || native_unc) {
    const Count start = extended_unc ? Count(8) : Count(2);
    Count separators = 0;
    for (Count index = start; index < work.get_size(); index++) {
      if (work[index] == L'\\' || work[index] == L'/') {
        separators++;
        if (separators == 2) {
          return copy_units(work.get_data(), index + 1);
        }
      }
    }

    if (separators == 1) {
      return copy_units(work.get_data(), work.get_size());
    }
  }

  return {};
}

#endif

// Anchors provide stable process locations rather than repeating mutable
// environment lookups for every emission. Capturing them during module
// initialization prevents a later `chdir` or environment edit from retargeting
// an existing canonical Path. Native strings stay private here because
// Path::Native provides the public materialization boundary.
class AnchorBindings {
 public:
  AnchorBindings() {

#ifdef TOOLCHAIN_LINUX

    root = copy_units("/", 1);
    work = capture_current_directory();
    exec = capture_executable_directory();
    temp = capture_environment_directory("TMPDIR");
    if (temp.is_empty()) {
      temp = copy_units("/tmp", 4);
    }

    home = capture_home_directory();

#elifdef TOOLCHAIN_WINDOWS

    work = capture_current_directory();
    root = capture_root(work.get_view());
    temp = capture_temporary_directory();
    home = capture_home_directory();
    exec = capture_executable_directory();

#endif
  }

  auto get(Filesystem::Path::Anchor anchor) const
      -> View::Vector<Filesystem::Path::NativeUnit> {
    switch (anchor) {
    case Filesystem::Path::Anchor::Root:
      return root;
    case Filesystem::Path::Anchor::Temp:
      return temp;
    case Filesystem::Path::Anchor::Home:
      return home;
    case Filesystem::Path::Anchor::Work:
      return work;
    case Filesystem::Path::Anchor::Exec:
      return exec;
    }

    return {};
  }

 private:
  NativeUnits root;
  NativeUnits temp;
  NativeUnits home;
  NativeUnits work;
  NativeUnits exec;
};

static AnchorBindings anchor_bindings;

#ifdef TOOLCHAIN_LINUX

auto Filesystem::Path::emit() const
    -> Storage::Result<Native, EmissionFailure> {
  const View::Vector<NativeUnit> base = anchor_bindings.get(anchor);
  if (base.is_empty()) {
    return EmissionFailure::AnchorUnavailable;
  }

  const Bool separator =
      !components.is_empty() && base[base.get_size() - 1] != '/';
  const Count required_size = base.get_size() +
                              (separator ? Count(1) : Count(0)) +
                              components.get_size() + 1;
  Global::Vector<NativeUnit> native;
  native.resize(required_size);

  Count destination = 0;
  for (Count index = 0; index < base.get_size(); index++) {
    native.get_data()[destination++] = base[index];
  }

  if (separator) {
    native.get_data()[destination++] = '/';
  }

  if (!components.is_empty()) {
    memcpy(
        native.get_data() + destination, components.get_data(),
        components.get_size());
    destination += components.get_size();
  }

  native.get_data()[destination] = 0;
  return Native(Data::take(native));
}

#elifdef TOOLCHAIN_WINDOWS

auto Filesystem::Path::emit() const
    -> Storage::Result<Native, EmissionFailure> {
  constexpr Count max_conversion_size = 0x7FFF'FFFF;
  const View::Vector<NativeUnit> base = anchor_bindings.get(anchor);
  if (base.is_empty()) {
    return EmissionFailure::AnchorUnavailable;
  }

  if (components.get_size() > max_conversion_size) {
    return EmissionFailure::HostLimit;
  }

  S32 component_size = 0;
  if (!components.is_empty()) {
    component_size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS,
        Data::cast<const char>(components.get_data()),
        S32(components.get_size()), nullptr, 0);
    if (component_size <= 0) {
      return EmissionFailure::HostLimit;
    }
  }

  const Bool already_long = base.get_size() >= 4 && base[0] == L'\\' &&
                            base[1] == L'\\' && base[2] == L'?' &&
                            base[3] == L'\\';
  const Bool unc = !already_long && base.get_size() >= 2 && base[0] == L'\\' &&
                   base[1] == L'\\';
  const Count prefix_size =
      already_long ? Count(0) : (unc ? Count(8) : Count(4));
  const Count base_start = unc ? Count(2) : Count(0);
  const Count copied_base_size = base.get_size() - base_start;
  const NativeUnit last_base = base[base.get_size() - 1];
  const Bool separator =
      !components.is_empty() && last_base != L'\\' && last_base != L'/';
  const Count required_size = prefix_size + copied_base_size +
                              (separator ? Count(1) : Count(0)) +
                              Count(component_size) + 1;

  Global::Vector<NativeUnit> native;
  native.resize(required_size);

  Count destination = 0;
  if (!already_long) {
    // Extended drive paths keep their drive spelling. UNC paths replace the
    // leading pair of separators with the distinct `\\?\UNC\` prefix.
    const wchar_t* prefix = unc ? L"\\\\?\\UNC\\" : L"\\\\?\\";
    for (Count index = 0; index < prefix_size; index++) {
      native.get_data()[destination++] = prefix[index];
    }
  }

  for (Count index = base_start; index < base.get_size(); index++) {
    const wchar_t unit = base[index];
    native.get_data()[destination++] = unit == L'/' ? L'\\' : unit;
  }

  if (separator) {
    native.get_data()[destination++] = L'\\';
  }

  if (component_size != 0) {
    const S32 written = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS,
        Data::cast<const char>(components.get_data()),
        S32(components.get_size()), native.get_data() + destination,
        component_size);
    if (written != component_size) {
      return EmissionFailure::HostLimit;
    }

    for (Count index = 0; index < Count(component_size); index++) {
      if (native.get_data()[destination + index] == L'/') {
        native.get_data()[destination + index] = L'\\';
      }
    }

    destination += component_size;
  }

  native.get_data()[destination] = 0;
  return Native(Data::take(native));
}

#else

auto Filesystem::Path::emit() const
    -> Storage::Result<Native, EmissionFailure> {
  return EmissionFailure::UnsupportedHost;
}

#endif
