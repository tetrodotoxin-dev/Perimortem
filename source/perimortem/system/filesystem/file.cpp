// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/filesystem/file.hpp"

#include <errno.h>
#include <stdio.h>

#ifdef TOOLCHAIN_LINUX
#include <sys/stat.h>
#elifdef TOOLCHAIN_WINDOWS
#include <io.h>
#include <windows.h>
#endif

#include "perimortem/core/data.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/memory/managed/bytes.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;

static constexpr Count max_logged_path_size = 512;
static constexpr Count max_warning_size = max_logged_path_size + 256;
static constexpr Count max_read_size = Count(1) << 36;

enum class FileKind {
  Missing,
  Regular,
  Directory,
  Other,
};

// Native strings already carry the encoding, terminator and Windows extended
// length prefix established by Path. Only the selected host operation differs
// here.
static auto open_stream(const Filesystem::Path::Native& path, const char* mode)
    -> FILE* {

#ifdef TOOLCHAIN_LINUX

  return fopen(path.get_data(), mode);

#elifdef TOOLCHAIN_WINDOWS

  return _wfopen(path.get_data(), mode[0] == 'r' ? L"rb" : L"wb");

#else

  errno = ENOSYS;
  return nullptr;

#endif
}

static auto replace_path(
    const Filesystem::Path::Native& source,
    const Filesystem::Path::Native& destination) -> S32 {

#ifdef TOOLCHAIN_LINUX

  return rename(source.get_data(), destination.get_data());

#elifdef TOOLCHAIN_WINDOWS

  if (MoveFileExW(
          source.get_data(), destination.get_data(),
          MOVEFILE_REPLACE_EXISTING)) {
    return 0;
  }

  errno = EIO;
  return -1;

#else

  errno = ENOSYS;
  return -1;

#endif
}

static auto remove_path(const Filesystem::Path::Native& path) -> S32 {

#ifdef TOOLCHAIN_LINUX

  return ::remove(path.get_data());

#elifdef TOOLCHAIN_WINDOWS

  return _wremove(path.get_data());

#else

  errno = ENOSYS;
  return -1;

#endif
}

static auto inspect_path(const Filesystem::Path::Native& path) -> FileKind {

#ifdef TOOLCHAIN_LINUX

  struct stat64 status;
  if (stat64(path.get_data(), &status) != 0) {
    return FileKind::Missing;
  }

  if (S_ISREG(status.st_mode)) {
    return FileKind::Regular;
  }

  return S_ISDIR(status.st_mode) ? FileKind::Directory : FileKind::Other;

#elifdef TOOLCHAIN_WINDOWS

  const U32 attributes = GetFileAttributesW(path.get_data());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    return FileKind::Missing;
  }

  if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
    return FileKind::Directory;
  }

  return attributes & FILE_ATTRIBUTE_DEVICE ? FileKind::Other
                                            : FileKind::Regular;

#else

  errno = ENOSYS;
  return FileKind::Missing;

#endif
}

// Metadata comes from the open stream, not a second lookup of its pathname.
// Windows supplies a volume and file index instead of device and inode, and
// its timestamp units are converted to the same seconds and nanoseconds form.
static auto inspect_stream(FILE* file)
    -> Storage::Option<Filesystem::File::Fingerprint> {

#ifdef TOOLCHAIN_LINUX

  struct stat64 status;
  if (fstat64(fileno(file), &status) != 0) {
    return {};
  }

  if (!S_ISREG(status.st_mode) || status.st_size < 0) {
    errno = EINVAL;
    return {};
  }

  return Filesystem::File::Fingerprint(
      U64(status.st_dev), U64(status.st_ino), U64(status.st_size),
      S64(status.st_mtim.tv_sec), S64(status.st_mtim.tv_nsec),
      S64(status.st_ctim.tv_sec), S64(status.st_ctim.tv_nsec));

#elifdef TOOLCHAIN_WINDOWS

  const auto handle =
      Data::restore<HANDLE>(CppSize(_get_osfhandle(_fileno(file))));
  BY_HANDLE_FILE_INFORMATION identity;
  FILE_BASIC_INFO times;
  if (!GetFileInformationByHandle(handle, &identity) ||
      !GetFileInformationByHandleEx(
          handle, FileBasicInfo, &times, sizeof(times))) {
    errno = EIO;
    return {};
  }

  if (identity.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ||
      GetFileType(handle) != FILE_TYPE_DISK) {
    errno = EINVAL;
    return {};
  }

  constexpr S64 ticks_per_second = 10'000'000;
  constexpr S64 epoch_seconds = 11'644'473'600;
  const S64 modified = times.LastWriteTime.QuadPart;
  const S64 changed = times.ChangeTime.QuadPart;
  return Filesystem::File::Fingerprint(
      identity.dwVolumeSerialNumber,
      (U64(identity.nFileIndexHigh) << 32) | identity.nFileIndexLow,
      (U64(identity.nFileSizeHigh) << 32) | identity.nFileSizeLow,
      modified / ticks_per_second - epoch_seconds,
      modified % ticks_per_second * 100,
      changed / ticks_per_second - epoch_seconds,
      changed % ticks_per_second * 100);

#else

  errno = ENOSYS;
  return {};

#endif
}

// Every diagnostic stage in one filesystem transaction uses the same operation
// name. This keeps its messages consistent from emission through close.
static constexpr View::Bytes file_read_operation =
    "System::Filesystem::File read"_view;
static constexpr View::Bytes file_fingerprint_operation =
    "System::Filesystem::File fingerprint"_view;
static constexpr View::Bytes file_write_operation =
    "System::Filesystem::File write"_view;
static constexpr View::Bytes file_replace_operation =
    "System::Filesystem::File replace"_view;

// Keeps diagnostic storage bounded while preserving the canonical anchor,
// useful component prefix and exact transaction stage. Native drive, home and
// temporary spellings stay private to Path emission.
static auto log_file_warning(
    View::Bytes operation,
    const Filesystem::Path& path,
    View::Bytes stage,
    View::Bytes detail_name,
    S64 detail) -> void {
  const View::Bytes components =
      path.get_components().slice(0, max_logged_path_size);
  Diagnostics::Log::Message<max_warning_size> warning(
      Diagnostics::Log::Level::Warning, Diagnostics::Source());
  warning << operation << " failed. path=["_view << path.get_anchor_name()
          << "]:/"_view << components;
  if (components.get_size() != path.get_components().get_size()) {
    warning << "..."_view;
  }

  warning << " stage="_view << stage << ' ' << detail_name << '=' << detail;
}

// Read type and size from the same open stream later consumed for content. A
// concurrent pathname replacement cannot switch objects between metadata
// inspection and the file read.
static auto get_file_fingerprint(
    FILE* file,
    View::Bytes operation,
    const Filesystem::Path& path)
    -> Storage::Option<Filesystem::File::Fingerprint> {
  const auto accept_fingerprint =
      [](const Filesystem::File::Fingerprint& fingerprint)
      -> Storage::Option<Filesystem::File::Fingerprint> { return fingerprint; };
  const auto report_failure =
      [&]() -> Storage::Option<Filesystem::File::Fingerprint> {
    log_file_warning(operation, path, "metadata"_view, "errno"_view, errno);
    return {};
  };

  return inspect_stream(file).visit(accept_fingerprint, report_failure);
}

// Global and Managed destinations receive file values directly into their final
// allocation. The Arena overload returns a View over that allocation which
// remains accessible until its Arena resets or is destroyed.
template <typename bytes_type>
static auto read_file(
    FILE* file,
    bytes_type& data,
    Filesystem::File::Fingerprint& fingerprint,
    View::Bytes operation,
    const Filesystem::Path& path) -> Bool {
  const auto read_content = [&](const Filesystem::File::Fingerprint& selected) {
    const Count size = selected.get_size();
    if (size > max_read_size) {
      log_file_warning(operation, path, "size"_view, "value"_view, S64(size));
      return False;
    }

    fingerprint = selected;

    // The file read replaces every requested byte. Forgetful resize avoids
    // clearing storage that cannot be observed before that overwrite completes.
    data.forgetful_resize(size);
    if (size == 0) {
      return True;
    }

    const CppSize items_read =
        fread(data.get_access().get_data(), 1, CppSize(size), file);
    if (items_read != CppSize(size) || ferror(file) != 0) {
      const S32 read_error = errno;
      log_file_warning(
          operation, path, "content"_view, "errno"_view, read_error);
      return False;
    }

    return True;
  };

  const auto fingerprint_failed = []() { return False; };
  return get_file_fingerprint(file, operation, path)
      .visit(read_content, fingerprint_failed);
}

// Completes the content stage against an already opened stream. Empty content
// succeeds because the selected open mode already established truncation.
static auto write_file(
    FILE* file,
    View::Bytes data,
    View::Bytes operation,
    const Filesystem::Path& path) -> Bool {
  if (data.is_empty()) {
    return True;
  }

  const CppSize items_written =
      fwrite(data.get_data(), data.get_size(), 1, file);
  if (items_written != 1) {
    const S32 write_error = errno;
    log_file_warning(
        operation, path, "content"_view, "errno"_view, write_error);
    return False;
  }

  return True;
}

// Closure is part of a file transaction because buffered input or output can
// still report a failure while the stream is being released.
static auto close_stream(
    FILE* file,
    View::Bytes operation,
    const Filesystem::Path& path) -> Bool {
  const S32 closed = fclose(file);
  if (closed == 0) {
    return True;
  }

  const S32 close_error = errno;
  log_file_warning(operation, path, "close"_view, "errno"_view, close_error);
  return False;
}

template <typename bytes_type>
static auto read_file(
    const Filesystem::Path& path,
    bytes_type& data,
    Filesystem::File::Fingerprint& fingerprint) -> Bool {
  const auto read_native = [&](const Filesystem::Path::Native& native) {
    // Open the emitted path exactly once. Metadata, content and close all use
    // this stream when the pathname later changes.
    FILE* file = open_stream(native, "rb");
    if (!file) {
      const S32 open_error = errno;
      log_file_warning(
          file_read_operation, path, "open"_view, "errno"_view, open_error);
      return False;
    }

    const Bool read =
        read_file(file, data, fingerprint, file_read_operation, path);
    const Bool closed = close_stream(file, file_read_operation, path);
    return Bool(read && closed);
  };

  const auto emission_failed = [&](Filesystem::Path::EmissionFailure failure) {
    log_file_warning(
        file_read_operation, path, "emit"_view, "failure"_view,
        S64(U8(failure)));
    return False;
  };

  return path.emit().visit(read_native, emission_failed);
}

auto Filesystem::File::read(const Path& path)
    -> Storage::Option<Global::Bytes> {
  Global::Bytes data;
  Fingerprint fingerprint;
  if (!read_file(path, data, fingerprint)) {
    return {};
  }

  return Data::take(data);
}

auto Filesystem::File::fingerprint(const Path& path)
    -> Storage::Option<Fingerprint> {
  const auto inspect_native =
      [&](const Path::Native& native) -> Storage::Option<Fingerprint> {
    FILE* file = open_stream(native, "rb");
    if (!file) {
      const S32 open_error = errno;
      log_file_warning(
          file_fingerprint_operation, path, "open"_view, "errno"_view,
          open_error);
      return {};
    }

    auto selected =
        get_file_fingerprint(file, file_fingerprint_operation, path);
    const Bool closed = close_stream(file, file_fingerprint_operation, path);
    return closed ? selected : Storage::Option<Fingerprint>();
  };

  const auto emission_failed =
      [&](Path::EmissionFailure failure) -> Storage::Option<Fingerprint> {
    log_file_warning(
        file_fingerprint_operation, path, "emit"_view, "failure"_view,
        S64(U8(failure)));
    return {};
  };

  return path.emit().visit(inspect_native, emission_failed);
}

auto Filesystem::File::read(Allocator::Arena& arena, const Path& path)
    -> Storage::Option<View::Bytes> {
  // The caller selects Arena storage when file values join an existing
  // transaction lifetime. Reading directly into that Arena avoids a second
  // process heap allocation.
  Managed::Bytes data{arena};
  Fingerprint fingerprint;
  if (!read_file(path, data, fingerprint)) {
    return {};
  }

  return Storage::Option<View::Bytes>(data.get_view());
}

auto Filesystem::File::write(View::Bytes data, const Path& path) -> Bool {
  const auto write_native = [&](const Path::Native& native) {
    // Open once for replacement, write through that stream, then treat closure
    // as part of the operation result.
    FILE* file = open_stream(native, "wb");
    if (!file) {
      const S32 open_error = errno;
      log_file_warning(
          file_write_operation, path, "open"_view, "errno"_view, open_error);
      return False;
    }

    const Bool written = write_file(file, data, file_write_operation, path);
    const Bool closed = close_stream(file, file_write_operation, path);
    return Bool(written && closed);
  };

  const auto emission_failed = [&](Path::EmissionFailure failure) {
    log_file_warning(
        file_write_operation, path, "emit"_view, "failure"_view,
        S64(U8(failure)));
    return False;
  };

  return path.emit().visit(write_native, emission_failed);
}

auto Filesystem::File::replace(const Path& source, const Path& destination)
    -> Bool {
  const auto replace_source = [&](const Path::Native& native_source) {
    auto emitted_destination = destination.emit();
    const auto replace_destination =
        [&](const Path::Native& native_destination) {
          if (replace_path(native_source, native_destination) == 0) {
            return True;
          }

          const S32 replace_error = errno;
          log_file_warning(
              file_replace_operation, destination, "rename"_view, "errno"_view,
              replace_error);
          return False;
        };

    const auto destination_emission_failed =
        [&](Path::EmissionFailure failure) {
          log_file_warning(
              file_replace_operation, destination, "emit"_view, "failure"_view,
              S64(U8(failure)));
          return False;
        };

    return emitted_destination.visit(
        replace_destination, destination_emission_failed);
  };

  const auto source_emission_failed = [&](Path::EmissionFailure failure) {
    log_file_warning(
        file_replace_operation, source, "emit"_view, "failure"_view,
        S64(U8(failure)));
    return False;
  };

  return source.emit().visit(replace_source, source_emission_failed);
}

auto Filesystem::File::remove(const Path& path) -> Bool {
  const auto remove_native = [](const Path::Native& native) {
    return Bool(remove_path(native) == 0);
  };

  const auto emission_failed = [](Path::EmissionFailure) { return False; };
  return path.emit().visit(remove_native, emission_failed);
}

auto Filesystem::File::exists(const Path& path) -> Bool {
  const auto inspect_native = [](const Path::Native& native) {
    return Bool(inspect_path(native) == FileKind::Regular);
  };

  const auto emission_failed = [](Path::EmissionFailure) { return False; };
  return path.emit().visit(inspect_native, emission_failed);
}
