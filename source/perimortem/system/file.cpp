// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/file.hpp"

#include <errno.h>
#include <stdio.h>

#ifdef PERI_LINUX
#include <sys/stat.h>
#elifdef PERI_WINDOWS
#include <io.h>
#include <windows.h>
#endif

#include "perimortem/core/static/bytes.hpp"
#include "perimortem/core/data.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/null_terminated.hpp"

#include "perimortem/memory/managed/bytes.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;

// Paths and diagnostics stay bounded without allocating a temporary string
// for every transaction. Windows converts this same UTF8 spelling at the host
// boundary, while Linux consumes its bytes directly.
static constexpr Count max_path_size = 512;
static constexpr Count max_warning_size = max_path_size + 256;
static constexpr Count max_read_size = Count(1) << 35;

enum class FileKind {
  Missing,
  Regular,
  Directory,
  Other,
};

#ifdef PERI_WINDOWS

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

#ifdef PERI_LINUX

  return fopen(path, mode);

#elifdef PERI_WINDOWS

  wchar_t wide[max_path_size];
  return wide_path(path, wide) ? _wfopen(wide, mode[0] == 'r' ? L"rb" : L"wb")
                               : nullptr;

#else

  errno = ENOSYS;
  return nullptr;

#endif

}

static auto replace_path(const char* source, const char* destination) -> S32 {

#ifdef PERI_LINUX

  return rename(source, destination);

#elifdef PERI_WINDOWS

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

#ifdef PERI_LINUX

  return ::remove(path);

#elifdef PERI_WINDOWS

  wchar_t wide[max_path_size];
  return wide_path(path, wide) ? _wremove(wide) : -1;

#else

  errno = ENOSYS;
  return -1;

#endif

}

static auto inspect_path(const char* path) -> FileKind {

#ifdef PERI_LINUX

  struct stat64 status;
  if (stat64(path, &status) != 0) {
    return FileKind::Missing;
  }

  if (S_ISREG(status.st_mode)) {
    return FileKind::Regular;
  }

  return S_ISDIR(status.st_mode) ? FileKind::Directory : FileKind::Other;

#elifdef PERI_WINDOWS

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
static auto inspect_stream(FILE* file) -> Option<File::Fingerprint> {

#ifdef PERI_LINUX

  struct stat64 status;
  if (fstat64(fileno(file), &status) != 0) {
    return {};
  }

  if (!S_ISREG(status.st_mode) || status.st_size < 0) {
    errno = EINVAL;
    return {};
  }

  return File::Fingerprint(
      U64(status.st_dev), U64(status.st_ino), U64(status.st_size),
      S64(status.st_mtim.tv_sec), S64(status.st_mtim.tv_nsec),
      S64(status.st_ctim.tv_sec), S64(status.st_ctim.tv_nsec));

#elifdef PERI_WINDOWS

  const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(file)));
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
  return File::Fingerprint(
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

// Operation identities are part of the diagnostic contract. Every stage of
// one filesystem transaction uses the same value so its messages cannot drift.
static constexpr View::Bytes file_read_operation = "System::File read"_view;
static constexpr View::Bytes file_write_operation = "System::File write"_view;
static constexpr View::Bytes file_replace_operation =
    "System::File replace"_view;

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

// Classifies and sizes the same opened object that will provide the content.
// Reading metadata through its descriptor prevents a pathname replacement from
// changing which object the transaction observes.
static auto get_file_fingerprint(
    FILE* file,
    View::Bytes operation,
    View::Bytes path,
    FailureReporting reporting) -> Option<File::Fingerprint> {
  auto fingerprint = inspect_stream(file);
  if (!fingerprint) {
    report_file_warning(
        reporting, operation, path, "metadata"_view, "errno"_view, errno);
    return {};
  }

  if (fingerprint->get_size() > max_read_size) {
    report_file_warning(
        reporting, operation, path, "size"_view, "value"_view,
        S64(fingerprint->get_size()));
    return {};
  }

  return fingerprint;
}

// Both byte owners receive content directly into their final allocation. The
// Arena overload can then lend those bytes without copying a temporary buffer.
template <typename bytes_type>
static auto read_file(
    FILE* file,
    bytes_type& data,
    File::Fingerprint& fingerprint,
    View::Bytes operation,
    View::Bytes path,
    FailureReporting reporting) -> Bool {
  auto selected = get_file_fingerprint(file, operation, path, reporting);
  if (!selected) {
    return False;
  }

  fingerprint = *selected;
  Count size = selected->get_size();
  data.resize(size);
  if (size == 0) {
    return True;
  }

  CppSize items_read =
      fread(data.get_access().get_data(), 1, CppSize(size), file);
  if (items_read != CppSize(size) || ferror(file) != 0) {
    S32 read_error = errno;
    report_file_warning(
        reporting, operation, path, "content"_view, "errno"_view, read_error);
    return False;
  }

  return True;
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
    -> Option<View::Bytes> {
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

// Join spellings without resolving parent segments ourselves. Resolving a link
// before `..` can select a different directory than lexical normalization
// would. The filesystem owns that decision, just as it does for an ordinary
// File path.
static auto member_path(
    Access::Bytes output,
    View::Bytes directory,
    View::Bytes member) -> Option<View::Bytes> {
  Static::Bytes<max_path_size> member_buffer;
  auto path = create_path(member_buffer, member);
  if (!path || path->is_empty() || directory.is_empty()) {
    return {};
  }

  Bool absolute = (*path)[0] == '/';

#ifdef PERI_WINDOWS

  absolute |= path->get_size() >= 2 && (*path)[1] == ':';

#endif

  if (absolute) {
    return create_path(output, *path);
  }

  const Count prefix = directory.get_size();
  const Count suffix = path->get_size();
  if (prefix + 1 + suffix >= output.get_size()) {
    return {};
  }

  Data::copy(output.get_data(), directory.get_data(), prefix);
  output.get_data()[prefix] = '/';
  Data::copy(output.get_data() + prefix + 1, path->get_data(), suffix);
  output.get_data()[prefix + 1 + suffix] = 0;
  return View::Bytes(output.get_data(), prefix + 1 + suffix);
}

template <typename bytes_type>
static auto read_file(
    View::Bytes location,
    bytes_type& data,
    File::Fingerprint& fingerprint,
    FailureReporting reporting = FailureReporting::Warning) -> Bool {
  // Serialize the caller path into bounded native storage before any
  // filesystem operation begins.
  Static::Bytes<max_path_size> path_buffer;
  auto path = create_path(path_buffer, location);
  if (!path) {
    report_file_warning(
        reporting, file_read_operation, location, "path"_view, "size"_view,
        S64(location.get_size()));
    return False;
  }

  // Open the selected path exactly once. Metadata, content, and close
  // below all describe this stream even if the pathname later changes.
  const char* native_path = Data::cast<const char>((*path).get_data());
  FILE* file = open_stream(native_path, "rb");
  if (!file) {
    S32 open_error = errno;
    report_file_warning(
        reporting, file_read_operation, location, "open"_view, "errno"_view,
        open_error);
    return False;
  }

  // Fill the storage selected by the public overload and include
  // stream closure in the reported result.
  Bool read = read_file(
      file, data, fingerprint, file_read_operation, location, reporting);
  Bool closed = close_stream(file, file_read_operation, location, reporting);

  return read && closed;
}

auto File::Root::open(View::Bytes location) -> Option<Root> {
  Static::Bytes<max_path_size> buffer;
  auto path = create_path(buffer, location);
  if (!path || path->is_empty() ||
      inspect_path(Data::cast<const char>(path->get_data())) !=
          FileKind::Directory) {
    return {};
  }

  return Root(*path);
}

auto File::Root::read(View::Bytes relative_path) const
    -> Option<Dynamic::Bytes> {
  auto snapshot = read_snapshot(relative_path);
  return snapshot ? Option<Dynamic::Bytes>(snapshot->take_contents())
                  : Option<Dynamic::Bytes>();
}

auto File::Root::read_snapshot(View::Bytes relative_path) const
    -> Option<File::Snapshot> {
  Static::Bytes<max_path_size> buffer;
  auto path = member_path(buffer, location, relative_path);
  Dynamic::Bytes data;
  File::Fingerprint fingerprint;
  if (!path || !read_file(*path, data, fingerprint, FailureReporting::Silent)) {
    return {};
  }

  return File::Snapshot(Data::take(data), fingerprint);
}

auto File::Root::fingerprint(View::Bytes relative_path) const
    -> Option<File::Fingerprint> {
  Static::Bytes<max_path_size> buffer;
  auto path = member_path(buffer, location, relative_path);
  if (!path) {
    return {};
  }

  FILE* file = open_stream(Data::cast<const char>(path->get_data()), "rb");
  if (!file) {
    return {};
  }

  auto selected =
      get_file_fingerprint(file, {}, *path, FailureReporting::Silent);
  Bool closed = close_stream(file, {}, *path, FailureReporting::Silent);
  return closed ? selected : Option<File::Fingerprint>();
}

auto File::Root::read(Allocator::Arena& arena, View::Bytes relative_path) const
    -> Option<View::Bytes> {
  Static::Bytes<max_path_size> buffer;
  auto path = member_path(buffer, location, relative_path);
  Managed::Bytes data(arena);
  File::Fingerprint fingerprint;
  if (!path || !read_file(*path, data, fingerprint, FailureReporting::Silent)) {
    return {};
  }

  return data.get_view();
}

auto File::Root::write(View::Bytes data, View::Bytes relative_path) const
    -> Bool {
  Static::Bytes<max_path_size> buffer;
  auto path = member_path(buffer, location, relative_path);
  return path && File::write(data, *path);
}

auto File::Root::remove(View::Bytes relative_path) const -> Bool {
  Static::Bytes<max_path_size> buffer;
  auto path = member_path(buffer, location, relative_path);
  return path && File::remove(*path);
}

auto File::Root::exists(View::Bytes relative_path) const -> Bool {
  Static::Bytes<max_path_size> buffer;
  auto path = member_path(buffer, location, relative_path);
  return path && File::exists(*path);
}

auto File::read(View::Bytes location) -> Option<Dynamic::Bytes> {
  // Dynamic storage gives this overload an independently owned result.
  Dynamic::Bytes data;
  File::Fingerprint fingerprint;
  Bool read = read_file(location, data, fingerprint);
  if (!read) {
    return {};
  }

  return Option<Dynamic::Bytes>(static_cast<Dynamic::Bytes&&>(data));
}

auto File::read(Allocator::Arena& arena, View::Bytes location)
    -> Option<View::Bytes> {
  // Arena storage returns stable bytes without introducing another owner.
  Managed::Bytes data(arena);
  File::Fingerprint fingerprint;
  Bool read = read_file(location, data, fingerprint);
  if (!read) {
    return {};
  }

  return Option<View::Bytes>(data.get_view());
}

auto File::write(View::Bytes data, View::Bytes location) -> Bool {
  // Serialize the caller path into bounded native storage.
  Static::Bytes<max_path_size> path_buffer;
  auto path = create_path(path_buffer, location);
  if (!path) {
    log_file_warning(
        file_write_operation, location, "path"_view, "size"_view,
        S64(location.get_size()));
    return False;
  }

  const char* native_path = Data::cast<const char>((*path).get_data());

  // Open once for replacement, write through that stream, then treat
  // closure as part of the operation result.
  FILE* file = open_stream(native_path, "wb");
  if (!file) {
    S32 open_error = errno;
    log_file_warning(
        file_write_operation, location, "open"_view, "errno"_view, open_error);
    return False;
  }

  Bool written = write_file(file, data, file_write_operation, location);
  Bool closed = close_stream(
      file, file_write_operation, location, FailureReporting::Warning);

  return written && closed;
}

auto File::replace(View::Bytes source, View::Bytes destination) -> Bool {
  Static::Bytes<max_path_size> source_buffer;
  Static::Bytes<max_path_size> destination_buffer;
  auto source_path = create_path(source_buffer, source);
  auto destination_path = create_path(destination_buffer, destination);
  if (!source_path || !destination_path) {
    View::Bytes failed = source_path ? destination : source;
    log_file_warning(
        file_replace_operation, failed, "path"_view, "size"_view,
        S64(failed.get_size()));
    return False;
  }

  const char* native_source = Data::cast<const char>(source_path->get_data());
  const char* native_destination =
      Data::cast<const char>(destination_path->get_data());
  S32 replaced = replace_path(native_source, native_destination);
  if (replaced == 0) {
    return True;
  }

  S32 replace_error = errno;
  log_file_warning(
      file_replace_operation, destination, "rename"_view, "errno"_view,
      replace_error);
  return False;
}

auto File::remove(View::Bytes location) -> Bool {
  Static::Bytes<max_path_size> path_buffer;
  auto path = create_path(path_buffer, location);
  if (!path) {
    return False;
  }

  const char* native_path = Data::cast<const char>((*path).get_data());
  S32 removed = remove_path(native_path);
  return removed == 0;
}

auto File::exists(View::Bytes location) -> Bool {
  Static::Bytes<max_path_size> path_buffer;
  auto path = create_path(path_buffer, location);
  if (!path) {
    return False;
  }

  const char* native_path = Data::cast<const char>((*path).get_data());

  return inspect_path(native_path) == FileKind::Regular;
}
