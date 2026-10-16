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
#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/memory/global/bytes.hpp"
#include "perimortem/memory/managed/bytes.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;

// Paths and diagnostics stay bounded without allocating a temporary string
// for every transaction. Windows converts this same UTF8 spelling at the host
// boundary, while Linux consumes its bytes directly.
static constexpr Count max_path_size = 512;
static constexpr Count max_warning_size = max_path_size + 256;
static constexpr Count max_read_size = Count(1) << 36;

enum class FileKind {
  Missing,
  Regular,
  Directory,
  Other,
};

#ifdef TOOLCHAIN_WINDOWS

static auto wide_path(const char* path, wchar_t* output) -> Bool {
  if (MultiByteToWideChar(
          CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, output, max_path_size)) {
    return True;
  }

  errno = EILSEQ;
  return False;
}

#endif

// Only acquisition and native observation differ by host. Content transfer,
// allocation, and failure reporting below share the same transaction path.
static auto open_stream(const char* path, const char* mode) -> FILE* {

#ifdef TOOLCHAIN_LINUX

  return fopen(path, mode);

#elifdef TOOLCHAIN_WINDOWS

  wchar_t wide[max_path_size];
  return wide_path(path, wide) ? _wfopen(wide, mode[0] == 'r' ? L"rb" : L"wb")
                               : nullptr;

#else

  errno = ENOSYS;
  return nullptr;

#endif
}

static auto replace_path(const char* source, const char* destination) -> S32 {

#ifdef TOOLCHAIN_LINUX

  return rename(source, destination);

#elifdef TOOLCHAIN_WINDOWS

  wchar_t from[max_path_size];
  wchar_t to[max_path_size];
  if (!wide_path(source, from) || !wide_path(destination, to)) {
    return -1;
  }

  if (MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING)) {
    return 0;
  }

  errno = EIO;
  return -1;

#else

  errno = ENOSYS;
  return -1;

#endif
}

static auto remove_path(const char* path) -> S32 {

#ifdef TOOLCHAIN_LINUX

  return ::remove(path);

#elifdef TOOLCHAIN_WINDOWS

  wchar_t wide[max_path_size];
  return wide_path(path, wide) ? _wremove(wide) : -1;

#else

  errno = ENOSYS;
  return -1;

#endif
}

static auto inspect_path(const char* path) -> FileKind {

#ifdef TOOLCHAIN_LINUX

  struct stat64 status;
  if (stat64(path, &status) != 0) {
    return FileKind::Missing;
  }

  if (S_ISREG(status.st_mode)) {
    return FileKind::Regular;
  }

  return S_ISDIR(status.st_mode) ? FileKind::Directory : FileKind::Other;

#elifdef TOOLCHAIN_WINDOWS

  wchar_t wide[max_path_size];
  if (!wide_path(path, wide)) {
    return FileKind::Missing;
  }

  const DWORD attributes = GetFileAttributesW(wide);
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
// name. This keeps its messages consistent from open through close.
static constexpr View::Bytes file_read_operation =
    "System::Filesystem::File read"_view;
static constexpr View::Bytes file_write_operation =
    "System::Filesystem::File write"_view;
static constexpr View::Bytes file_replace_operation =
    "System::Filesystem::File replace"_view;

enum class FailureReporting {
  Silent,
  Warning,
};

// File operations prepare one bounded native path, open the selected object
// once, perform all metadata and content work against that opened object, then
// include closure in the result. Root only supplies the directory spelling
// used to find that object at the start of each transaction.

// Keeps diagnostic storage bounded while preserving the operation, path, and
// exact transaction stage that explain the failure.
static auto log_file_warning(
    View::Bytes operation,
    View::Bytes path,
    View::Bytes stage,
    View::Bytes detail_name,
    S64 detail) -> void {
  Count logged_path_size = path.get_size();
  if (logged_path_size != 0 && path[logged_path_size - 1] == '\0') {
    logged_path_size--;
  }

  if (logged_path_size > max_path_size) {
    logged_path_size = max_path_size;
  }

  View::Bytes logged_path(path.get_data(), logged_path_size);
  Diagnostics::Log::Message<max_warning_size> warning(
      Diagnostics::Log::Level::Warning, Diagnostics::Source());
  warning << operation << " failed. path="_view << logged_path << " stage="_view
          << stage << ' ' << detail_name << '=' << detail;
}

static auto report_file_warning(
    FailureReporting reporting,
    View::Bytes operation,
    View::Bytes path,
    View::Bytes stage,
    View::Bytes detail_name,
    S64 detail) -> void {
  if (reporting == FailureReporting::Warning) {
    log_file_warning(operation, path, stage, detail_name, detail);
  }
}

// Read type and size from the same open stream later consumed for content. A
// concurrent pathname replacement cannot switch objects between metadata
// inspection and the file read.
static auto get_file_fingerprint(
    FILE* file,
    View::Bytes operation,
    View::Bytes path,
    FailureReporting reporting)
    -> Storage::Option<Filesystem::File::Fingerprint> {
  return inspect_stream(file).visit(
      [&](const Filesystem::File::Fingerprint& fingerprint)
          -> Storage::Option<Filesystem::File::Fingerprint> {
        if (fingerprint.get_size() > max_read_size) {
          report_file_warning(
              reporting, operation, path, "size"_view, "value"_view,
              S64(fingerprint.get_size()));
          return {};
        }

        return fingerprint;
      },

      [&]() -> Storage::Option<Filesystem::File::Fingerprint> {
        report_file_warning(
            reporting, operation, path, "metadata"_view, "errno"_view, errno);
        return {};
      });
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
    View::Bytes path,
    FailureReporting reporting) -> Bool {
  return get_file_fingerprint(file, operation, path, reporting)
      .visit(
          [&](const Filesystem::File::Fingerprint& selected) {
            fingerprint = selected;

            const Count size = selected.get_size();
            // The file read replaces every requested byte. Forgetful resize
            // avoids clearing storage that cannot be observed before that
            // overwrite completes.
            data.forgetful_resize(size);
            if (size == 0) {
              return True;
            }

            CppSize items_read =
                fread(data.get_access().get_data(), 1, CppSize(size), file);
            if (items_read != CppSize(size) || ferror(file) != 0) {
              S32 read_error = errno;
              report_file_warning(
                  reporting, operation, path, "content"_view, "errno"_view,
                  read_error);
              return False;
            }

            return True;
          },

          []() { return False; });
}

// Completes the content stage against an already opened stream. Empty content
// succeeds because the selected open mode already established truncation.
static auto write_file(
    FILE* file,
    View::Bytes data,
    View::Bytes operation,
    View::Bytes path) -> Bool {
  if (data.is_empty()) {
    return True;
  }

  CppSize items_written = fwrite(data.get_data(), data.get_size(), 1, file);
  if (items_written != 1) {
    S32 write_error = errno;
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
    View::Bytes path,
    FailureReporting reporting) -> Bool {
  S32 closed = fclose(file);
  if (closed == 0) {
    return True;
  }

  S32 close_error = errno;
  report_file_warning(
      reporting, operation, path, "close"_view, "errno"_view, close_error);
  return False;
}

// Writes one slash normalized path and exactly one final null into caller
// storage. The returned view excludes the terminator so it retains ordinary
// byte semantics.
static auto create_path(Access::Bytes output, View::Bytes path)
    -> Storage::Option<View::Bytes> {
  Count content_size = path.get_size();
  if (content_size != 0 && path[content_size - 1] == '\0') {
    content_size--;
  }

  for (Count i = 0; i < content_size; i++) {
    if (path[i] == '\0') {
      return {};
    }
  }

  if (content_size >= output.get_size()) {
    return {};
  }

  auto* output_data = output.get_data();
  for (Count i = 0; i < content_size; i++) {
    output_data[i] = path[i] == '\\' ? '/' : path[i];
  }

  output_data[content_size] = '\0';
  return View::Bytes(output_data, content_size);
}

// Join spellings without resolving parent segments here. Resolving a link
// before `..` can select a different directory than lexical normalization
// would, so the host filesystem performs that resolution as it does for an
// ordinary File path.
static auto member_path(
    Access::Bytes output,
    View::Bytes directory,
    View::Bytes member) -> Storage::Option<View::Bytes> {
  Storage::Bytes<max_path_size> member_buffer;
  return create_path(member_buffer, member)
      .visit(
          [&](View::Bytes path) -> Storage::Option<View::Bytes> {
            if (path.is_empty() || directory.is_empty()) {
              return {};
            }

            Bool absolute = path[0] == '/';

#ifdef TOOLCHAIN_WINDOWS

            absolute |= path.get_size() >= 2 && path[1] == ':';

#endif

            if (absolute) {
              return create_path(output, path);
            }

            const Count prefix = directory.get_size();
            const Count suffix = path.get_size();
            if (prefix + 1 + suffix >= output.get_size()) {
              return {};
            }

            Data::copy(output.get_data(), directory.get_data(), prefix);
            output.get_data()[prefix] = '/';
            Data::copy(output.get_data() + prefix + 1, path.get_data(), suffix);
            output.get_data()[prefix + 1 + suffix] = 0;
            return View::Bytes(output.get_data(), prefix + 1 + suffix);
          },

          []() -> Storage::Option<View::Bytes> { return {}; });
}

template <typename bytes_type>
static auto read_file(
    View::Bytes location,
    bytes_type& data,
    Filesystem::File::Fingerprint& fingerprint,
    FailureReporting reporting = FailureReporting::Warning) -> Bool {
  // Serialize the caller path into bounded native storage before any
  // filesystem operation begins.
  Storage::Bytes<max_path_size> path_buffer;
  return create_path(path_buffer, location)
      .visit(
          [&](View::Bytes path) {
            // Open the selected path exactly once. Metadata, content, and close
            // all use this stream when the pathname later changes.
            const char* native_path = Data::cast<const char>(path.get_data());
            FILE* file = open_stream(native_path, "rb");
            if (!file) {
              S32 open_error = errno;
              report_file_warning(
                  reporting, file_read_operation, location, "open"_view,
                  "errno"_view, open_error);
              return False;
            }

            // Fill the storage selected by the public overload and include
            // stream closure in the reported result.
            Bool read = read_file(
                file, data, fingerprint, file_read_operation, location,
                reporting);
            Bool closed =
                close_stream(file, file_read_operation, location, reporting);
            return Bool(read && closed);
          },

          [&]() {
            report_file_warning(
                reporting, file_read_operation, location, "path"_view,
                "size"_view, S64(location.get_size()));
            return False;
          });
}

auto Filesystem::File::Root::open(View::Bytes location)
    -> Storage::Option<Root> {
  Storage::Bytes<max_path_size> buffer;
  return create_path(buffer, location)
      .visit(
          [](View::Bytes path) -> Storage::Option<Root> {
            if (path.is_empty() ||
                inspect_path(Data::cast<const char>(path.get_data())) !=
                    FileKind::Directory) {
              return {};
            }

            return Root(path);
          },

          []() -> Storage::Option<Root> { return {}; });
}

auto Filesystem::File::Root::read(View::Bytes relative_path) const
    -> Storage::Option<Global::Bytes> {
  return read_snapshot(relative_path)
      .visit(
          [](Snapshot& snapshot) -> Storage::Option<Global::Bytes> {
            return snapshot.take_contents();
          },

          []() -> Storage::Option<Global::Bytes> { return {}; });
}

auto Filesystem::File::Root::read_snapshot(View::Bytes relative_path) const
    -> Storage::Option<Filesystem::File::Snapshot> {
  Storage::Bytes<max_path_size> buffer;
  return member_path(buffer, location, relative_path)
      .visit(
          [&](View::Bytes path) -> Storage::Option<Filesystem::File::Snapshot> {
            Global::Bytes data{};
            Filesystem::File::Fingerprint fingerprint;
            if (!read_file(path, data, fingerprint, FailureReporting::Silent)) {
              return {};
            }

            return Filesystem::File::Snapshot(Data::take(data), fingerprint);
          },

          []() -> Storage::Option<Filesystem::File::Snapshot> { return {}; });
}

auto Filesystem::File::Root::fingerprint(View::Bytes relative_path) const
    -> Storage::Option<Filesystem::File::Fingerprint> {
  Storage::Bytes<max_path_size> buffer;
  return member_path(buffer, location, relative_path)
      .visit(
          [](View::Bytes path)
              -> Storage::Option<Filesystem::File::Fingerprint> {
            FILE* file =
                open_stream(Data::cast<const char>(path.get_data()), "rb");
            if (!file) {
              return {};
            }

            auto fingerprint =
                get_file_fingerprint(file, {}, path, FailureReporting::Silent);
            Bool closed =
                close_stream(file, {}, path, FailureReporting::Silent);
            return closed ? fingerprint
                          : Storage::Option<Filesystem::File::Fingerprint>();
          },

          []() -> Storage::Option<Filesystem::File::Fingerprint> {
            return {};
          });
}

auto Filesystem::File::Root::read(
    Allocator::Arena& arena,
    View::Bytes relative_path) const -> Storage::Option<View::Bytes> {
  Storage::Bytes<max_path_size> buffer;
  return member_path(buffer, location, relative_path)
      .visit(
          [&](View::Bytes path) -> Storage::Option<View::Bytes> {
            Managed::Bytes data{arena};
            Filesystem::File::Fingerprint fingerprint;
            if (!read_file(path, data, fingerprint, FailureReporting::Silent)) {
              return {};
            }

            return data.get_view();
          },

          []() -> Storage::Option<View::Bytes> { return {}; });
}

auto Filesystem::File::Root::write(View::Bytes data, View::Bytes relative_path)
    const -> Bool {
  Storage::Bytes<max_path_size> buffer;
  return member_path(buffer, location, relative_path)
      .visit(
          [&](View::Bytes path) { return Filesystem::File::write(data, path); },
          []() { return False; });
}

auto Filesystem::File::Root::remove(View::Bytes relative_path) const -> Bool {
  Storage::Bytes<max_path_size> buffer;
  return member_path(buffer, location, relative_path)
      .visit(
          [](View::Bytes path) { return Filesystem::File::remove(path); },
          []() { return False; });
}

auto Filesystem::File::Root::exists(View::Bytes relative_path) const -> Bool {
  Storage::Bytes<max_path_size> buffer;
  return member_path(buffer, location, relative_path)
      .visit(
          [](View::Bytes path) { return Filesystem::File::exists(path); },
          []() { return False; });
}

auto Filesystem::File::read(View::Bytes location)
    -> Storage::Option<Global::Bytes> {
  Global::Bytes data{};
  Filesystem::File::Fingerprint fingerprint;
  Bool read = read_file(location, data, fingerprint);
  if (!read) {
    return {};
  }

  return Storage::Option<Global::Bytes>(Data::take(data));
}

auto Filesystem::File::read(Allocator::Arena& arena, View::Bytes location)
    -> Storage::Option<View::Bytes> {
  // The caller selects Arena storage when the file values join an existing
  // transaction lifetime. Reading directly into that Arena avoids a second
  // process heap allocation.
  Managed::Bytes data{arena};
  Filesystem::File::Fingerprint fingerprint;
  Bool read = read_file(location, data, fingerprint);
  if (!read) {
    return {};
  }

  return Storage::Option<View::Bytes>(data.get_view());
}

auto Filesystem::File::write(View::Bytes data, View::Bytes location) -> Bool {
  Storage::Bytes<max_path_size> path_buffer;
  return create_path(path_buffer, location)
      .visit(
          [&](View::Bytes path) {
            const char* native_path = Data::cast<const char>(path.get_data());

            // Open once for replacement, write through that stream, then treat
            // closure as part of the operation result.
            FILE* file = open_stream(native_path, "wb");
            if (!file) {
              S32 open_error = errno;
              log_file_warning(
                  file_write_operation, location, "open"_view, "errno"_view,
                  open_error);
              return False;
            }

            Bool written =
                write_file(file, data, file_write_operation, location);
            Bool closed = close_stream(
                file, file_write_operation, location,
                FailureReporting::Warning);
            return Bool(written && closed);
          },

          [&]() {
            log_file_warning(
                file_write_operation, location, "path"_view, "size"_view,
                S64(location.get_size()));
            return False;
          });
}

auto Filesystem::File::replace(View::Bytes source, View::Bytes destination)
    -> Bool {
  Storage::Bytes<max_path_size> source_buffer;
  Storage::Bytes<max_path_size> destination_buffer;
  auto source_path = create_path(source_buffer, source);
  auto destination_path = create_path(destination_buffer, destination);
  return source_path.visit(
      [&](View::Bytes native_source_path) {
        return destination_path.visit(
            [&](View::Bytes native_destination_path) {
              const char* native_source =
                  Data::cast<const char>(native_source_path.get_data());
              const char* native_destination =
                  Data::cast<const char>(native_destination_path.get_data());
              S32 replaced = replace_path(native_source, native_destination);
              if (replaced == 0) {
                return True;
              }

              S32 replace_error = errno;
              log_file_warning(
                  file_replace_operation, destination, "rename"_view,
                  "errno"_view, replace_error);
              return False;
            },

            [&]() {
              log_file_warning(
                  file_replace_operation, destination, "path"_view, "size"_view,
                  S64(destination.get_size()));
              return False;
            });
      },

      [&]() {
        log_file_warning(
            file_replace_operation, source, "path"_view, "size"_view,
            S64(source.get_size()));
        return False;
      });
}

auto Filesystem::File::remove(View::Bytes location) -> Bool {
  Storage::Bytes<max_path_size> path_buffer;
  return create_path(path_buffer, location)
      .visit(
          [](View::Bytes path) {
            const char* native_path = Data::cast<const char>(path.get_data());
            return Bool(remove_path(native_path) == 0);
          },

          []() { return False; });
}

auto Filesystem::File::exists(View::Bytes location) -> Bool {
  Storage::Bytes<max_path_size> path_buffer;
  return create_path(path_buffer, location)
      .visit(
          [](View::Bytes path) {
            const char* native_path = Data::cast<const char>(path.get_data());
            return Bool(inspect_path(native_path) == FileKind::Regular);
          },

          []() { return False; });
}
