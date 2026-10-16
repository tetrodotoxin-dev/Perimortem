// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/filesystem/file.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toolchain/validation/test.hpp"

#include "perimortem/core/diagnostics/log.hpp"

#if defined(TOOLCHAIN_LINUX) || defined(TOOLCHAIN_WASM)
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef TOOLCHAIN_LINUX
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <sys/wait.h>
#elifdef TOOLCHAIN_WINDOWS
#include <windows.h>
#include <winioctl.h>
#endif

#include "perimortem/core/algorithm/search.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/system/filesystem/path.hpp"
#include "perimortem/system/random.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;
using namespace Toolchain::Validation;

static_assert(__is_same(
    decltype(Filesystem::File::read(View::Bytes())),
    Storage::Option<Global::Bytes>));
static_assert(requires(const Filesystem::File::Root& root) {
  requires __is_same(
      decltype(root.read(View::Bytes())), Storage::Option<Global::Bytes>);
});
static_assert(requires(Filesystem::File::Snapshot& snapshot) {
  requires __is_same(decltype(snapshot.take_contents()), Global::Bytes);
});

constexpr auto test_file = "system_file_test.json"_view;
constexpr auto test_output = "system_file_test_out.json"_view;
constexpr auto test_replacement = "system_file_test_replacement.json"_view;
constexpr auto test_contents = "{\"value\":42}"_view;
constexpr auto replacement_contents = "{\"other\":24}"_view;

template <typename value_type, typename visitor_type>
static auto expect_selected(
    Storage::Option<value_type>& selected,
    visitor_type visitor,
    Test::TestResult& result) -> void {
  selected.visit(visitor, [&] { EXPECT(false); });
}

template <typename value_type>
static auto expect_contents(
    Storage::Option<value_type>& selected,
    View::Bytes expected,
    Test::TestResult& result) -> void {
  expect_selected(
      selected, [&](const value_type& value) { EXPECT_TEXT(value, expected); },
      result);
}

static constexpr const char* test_output_path = "system_file_test_out.json";

#ifdef TOOLCHAIN_LINUX

static constexpr const char* test_replacement_path =
    "system_file_test_replacement.json";

#endif

static constexpr CppSize temporary_path_capacity = 256;
static constexpr Count captured_file_log_capacity = 1024;

static Diagnostics::Log::Level captured_file_log_level;
static Storage::Bytes<captured_file_log_capacity> captured_file_log;
static Count captured_file_log_size = 0;

static auto capture_file_log(
    Diagnostics::Log::Level level,
    View::Bytes message,
    const Diagnostics::Source&) -> void {
  captured_file_log_level = level;
  captured_file_log_size = message.get_size();
  captured_file_log = message;
}

static auto capture_next_file_log() -> void {
  captured_file_log_size = 0;
  Diagnostics::Log::set_sink(capture_file_log);
}

static auto file_warning_contains(View::Bytes message) -> Bool {
  if (captured_file_log_level != Diagnostics::Log::Level::Warning) {
    return False;
  }

  View::Bytes captured = captured_file_log.slice(0, captured_file_log_size);
  return Algorithm::Search::match(captured, message) != Count(-1);
}

// Linux syscall tracing provides deterministic mutation between open, metadata,
// and read. Keep that additional kernel oracle separate from the portable file
// behavior exercised on both hosts.

#ifdef TOOLCHAIN_LINUX

enum class TraceMutation {
  ReplacePath,
  TruncateAfterMetadata,
};

static auto is_open_call(S64 call) -> Bool {
  return call == SYS_open || call == SYS_openat || call == SYS_openat2;
}

static auto is_metadata_call(S64 call) -> Bool {
  return call == SYS_stat || call == SYS_fstat || call == SYS_newfstatat;
}

static auto traced_child(
    const char* path,
    View::Bytes expected,
    Bool expected_present,
    const char* root_path,
    const char* read_path,
    Bool arena_read) -> void {
  Storage::Option<Filesystem::File::Root> root;
  if (root_path) {
    root = Filesystem::File::Root::open(
        NullTerminated(root_path, temporary_path_capacity).get_view());
    if (!root) {
      _exit(21);
    }
  }

  if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) != 0) {
    _exit(20);
  }

  raise(SIGSTOP);

  const char* selected_path = read_path ? read_path : path;
  View::Bytes requested_path =
      NullTerminated(selected_path, temporary_path_capacity).get_view();
  if (arena_read) {
    Allocator::Arena arena;
    auto source = root.visit(
        [&](Filesystem::File::Root& selected_root) {
          return selected_root.read(arena, requested_path);
        },

        [&] { return Filesystem::File::read(arena, requested_path); });
    if (!expected_present) {
      _exit(source ? 1 : 0);
    }

    Bool matches = source.visit(
        [&](View::Bytes contents) {
          return contents == expected ? True : False;
        },

        [] { return False; });
    _exit(matches ? 0 : 1);
  }

  auto source = root.visit(
      [&](Filesystem::File::Root& selected_root) {
        return selected_root.read(requested_path);
      },

      [&] { return Filesystem::File::read(requested_path); });
  if (!expected_present) {
    _exit(source ? 1 : 0);
  }

  Bool matches = source.visit(
      [&](const Global::Bytes& contents) {
        return contents == expected ? True : False;
      },

      [] { return False; });
  _exit(matches ? 0 : 1);
}

static auto mutate_path(
    TraceMutation mutation,
    const char* path,
    const char* replacement) -> Bool {
  if (mutation == TraceMutation::ReplacePath) {
    S32 renamed = rename(replacement, path);
    return renamed == 0;
  }

  S32 truncated = truncate(path, 0);
  return truncated == 0;
}

static auto trace_read(
    const char* path,
    View::Bytes expected,
    Bool expected_present,
    TraceMutation mutation,
    const char* replacement = nullptr,
    const char* root_path = nullptr,
    const char* read_path = nullptr,
    Bool arena_read = False) -> Bool {
  pid_t child = fork();
  if (child == 0) {
    traced_child(
        path, expected, expected_present, root_path, read_path, arena_read);
  }

  if (child < 0) {
    return False;
  }

  // Stop at every child system call so the pathname can change at an exact
  // boundary without adding a production test seam.
  S32 status = 0;
  pid_t waited = waitpid(child, &status, 0);
  if (waited != child || !WIFSTOPPED(status)) {
    return False;
  }

  S64 option_result =
      ptrace(PTRACE_SETOPTIONS, child, nullptr, PTRACE_O_TRACESYSGOOD);
  if (option_result != 0) {
    kill(child, SIGKILL);
    waitpid(child, &status, 0);
    return False;
  }

  Bool entering = True;
  Bool opened = False;
  Bool mutated = False;
  while (true) {
    S64 trace_result = ptrace(PTRACE_SYSCALL, child, nullptr, nullptr);
    if (trace_result != 0) {
      kill(child, SIGKILL);
      waitpid(child, &status, 0);
      return False;
    }

    waited = waitpid(child, &status, 0);
    if (waited != child) {
      return False;
    }

    if (WIFEXITED(status)) {
      return mutated && WEXITSTATUS(status) == 0;
    }

    if (!WIFSTOPPED(status) || WSTOPSIG(status) != (SIGTRAP | 0x80)) {
      continue;
    }

    struct user_regs_struct registers;
    S64 registers_result = ptrace(PTRACE_GETREGS, child, nullptr, &registers);
    if (registers_result != 0) {
      kill(child, SIGKILL);
      waitpid(child, &status, 0);
      return False;
    }

    S64 call = S64(registers.orig_rax);
    if (entering && mutation == TraceMutation::TruncateAfterMetadata &&
        opened && call == SYS_read && !mutated) {
      mutated = mutate_path(mutation, path, replacement);
    }

    entering = !entering;
    if (entering) {
      S64 call_result = S64(registers.rax);
      if (is_open_call(call) && call_result >= 0) {
        opened = True;
        if (mutation == TraceMutation::ReplacePath && !mutated) {
          mutated = mutate_path(mutation, path, replacement);
        }
      } else if (is_metadata_call(call) && call_result == 0) {
        if (mutation == TraceMutation::ReplacePath && !opened && !mutated) {
          mutated = mutate_path(mutation, path, replacement);
        } else if (
            mutation == TraceMutation::TruncateAfterMetadata && opened &&
            !mutated) {
          mutated = mutate_path(mutation, path, replacement);
        }
      }
    }
  }
}

#endif

static auto build_member_path(
    char* output,
    CppSize output_size,
    const char* root,
    const char* member) -> Bool {
  S32 written = snprintf(output, output_size, "%s/%s", root, member);
  return written > 0 && CppSize(written) < output_size;
}

#ifdef TOOLCHAIN_WINDOWS

static auto native_create_directory(const char* path) -> Bool {
  return CreateDirectoryA(path, nullptr) != 0;
}

static auto native_current_directory(char* output, U32 capacity) -> Bool {
  const U32 size = GetCurrentDirectoryA(capacity, output);
  return size > 0 && size < capacity;
}

static auto native_change_directory(const char* path) -> Bool {
  return SetCurrentDirectoryA(path) != 0;
}

static auto native_is_directory(const char* path) -> Bool {
  const U32 attributes = GetFileAttributesA(path);
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static auto native_create_link(const char* path, const char* target, S32* error)
    -> Bool {
  U32 flags = native_is_directory(target) ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;

#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE

  flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;

#endif

  if (CreateSymbolicLinkA(path, target, flags)) {
    *error = 0;
    return True;
  }

  *error = GetLastError();
  return False;
}

static auto native_remove_tree(const char* path) -> Bool {
  U32 attributes = GetFileAttributesA(path);
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const U32 error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
  }

  const Bool directory = attributes & FILE_ATTRIBUTE_DIRECTORY;
  const Bool link = attributes & FILE_ATTRIBUTE_REPARSE_POINT;
  if (directory && !link) {
    char pattern[temporary_path_capacity * 2];
    S32 written = snprintf(pattern, sizeof(pattern), "%s/*", path);
    if (written <= 0 || CppSize(written) >= sizeof(pattern)) {
      return False;
    }

    WIN32_FIND_DATAA entry;
    HANDLE search = FindFirstFileA(pattern, &entry);
    if (search != INVALID_HANDLE_VALUE) {
      Bool removed = True;
      do {
        if (strcmp(entry.cFileName, ".") == 0 ||
            strcmp(entry.cFileName, "..") == 0) {
          continue;
        }

        char child[temporary_path_capacity * 2];
        if (!build_member_path(child, sizeof(child), path, entry.cFileName) ||
            !native_remove_tree(child)) {
          removed = False;
        }
      } while (FindNextFileA(search, &entry));

      FindClose(search);
      if (!removed) {
        return False;
      }
    }
  }

  if (attributes & FILE_ATTRIBUTE_READONLY) {
    SetFileAttributesA(path, attributes & ~FILE_ATTRIBUTE_READONLY);
  }

  return directory ? RemoveDirectoryA(path) != 0 : DeleteFileA(path) != 0;
}

static auto native_rename(const char* source, const char* destination) -> Bool {
  return MoveFileExA(source, destination, MOVEFILE_REPLACE_EXISTING) != 0;
}

#else

static auto native_create_directory(const char* path) -> Bool {
  return mkdir(path, 0700) == 0;
}

static auto native_current_directory(char* output, Count capacity) -> Bool {
  return getcwd(output, capacity) != nullptr;
}

static auto native_change_directory(const char* path) -> Bool {
  return chdir(path) == 0;
}

static auto native_create_link(const char* path, const char* target, S32* error)
    -> Bool {
  if (symlink(target, path) == 0) {
    *error = 0;
    return True;
  }

  *error = errno;
  return False;
}

static auto native_remove_tree(const char* path) -> Bool {
  struct stat info;
  if (lstat(path, &info) != 0) {
    return errno == ENOENT;
  }

  if (!S_ISDIR(info.st_mode) || S_ISLNK(info.st_mode)) {
    return unlink(path) == 0;
  }

  DIR* directory = opendir(path);
  if (!directory) {
    return False;
  }

  Bool removed = True;
  while (dirent* entry = readdir(directory)) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    char child[temporary_path_capacity * 2];
    if (!build_member_path(child, sizeof(child), path, entry->d_name) ||
        !native_remove_tree(child)) {
      removed = False;
    }
  }

  closedir(directory);
  return removed && rmdir(path) == 0;
}

static auto native_rename(const char* source, const char* destination) -> Bool {
  return rename(source, destination) == 0;
}

#endif

static auto create_temporary_directory(char* output, CppSize capacity) -> Bool {

#ifdef TOOLCHAIN_WINDOWS

  char temporary[temporary_path_capacity];
  const U32 size = GetTempPathA(sizeof(temporary), temporary);
  if (!size || size >= sizeof(temporary)) {
    return False;
  }

  S32 written = snprintf(
      output, capacity, "%sperimortem_file_%llx", temporary,
      Random::generate());

#else

  const char* temporary = getenv("TMPDIR");
  if (!temporary || !temporary[0]) {
    temporary = "/tmp";
  }

  const char* separator = temporary[strlen(temporary) - 1] == '/' ? "" : "/";
  S32 written = snprintf(
      output, capacity, "%s%sperimortem_file_%llx", temporary, separator,
      Random::generate());

#endif

  if (written <= 0 || CppSize(written) >= capacity) {
    return False;
  }

  return native_create_directory(output);
}

static auto cleanup_temporary_root(const char* root) -> void {
  if (!root[0]) {
    return;
  }

  native_remove_tree(root);
}

class TemporaryRoot {
 public:
  TemporaryRoot() {
    if (!create_temporary_directory(root_path, sizeof(root_path))) {
      return;
    }

    S32 written =
        snprintf(outside_path, sizeof(outside_path), "%s_outside", root_path);
    if (written <= 0 || CppSize(written) >= sizeof(outside_path)) {
      return;
    }

    written = snprintf(
        outside_directory_path, sizeof(outside_directory_path),
        "%s_outside_directory", root_path);
    valid = written > 0 && CppSize(written) < sizeof(outside_directory_path);
  }

  TemporaryRoot(const TemporaryRoot&) = delete;
  auto operator=(const TemporaryRoot&) -> TemporaryRoot& = delete;

  ~TemporaryRoot() {
    cleanup_temporary_root(root_path);
    cleanup_temporary_root(moved_path);
    cleanup_temporary_root(outside_directory_path);
    cleanup_temporary_root(outside_path);
  }

  operator bool() const { return bool(valid); }

  auto get_location() const -> View::Bytes {
    return NullTerminated(root_path, sizeof(root_path)).get_view();
  }

  auto get_path() const -> const char* { return root_path; }
  auto get_outside_path() const -> const char* { return outside_path; }
  auto get_outside_directory_path() const -> const char* {
    return outside_directory_path;
  }

  auto get_member_path(const char* member, char* output, CppSize output_size)
      const -> Bool {
    return build_member_path(output, output_size, root_path, member);
  }

  auto write_member(const char* member, View::Bytes data) const -> Bool {
    char path[temporary_path_capacity];
    Bool built = build_member_path(path, sizeof(path), root_path, member);
    if (!built) {
      return False;
    }

    return Filesystem::File::write(
        data, NullTerminated(path, sizeof(path)).get_view());
  }

  auto write_outside(View::Bytes data) const -> Bool {
    return Filesystem::File::write(
        data, NullTerminated(outside_path, sizeof(outside_path)).get_view());
  }

  auto create_outside_directory() const -> Bool {
    return native_create_directory(outside_directory_path);
  }

  auto write_outside_member(const char* member, View::Bytes data) const
      -> Bool {
    char path[temporary_path_capacity];
    Bool built =
        build_member_path(path, sizeof(path), outside_directory_path, member);
    if (!built) {
      return False;
    }

    return Filesystem::File::write(
        data, NullTerminated(path, sizeof(path)).get_view());
  }

  auto read_outside_member(const char* member) const
      -> Storage::Option<Global::Bytes> {
    char path[temporary_path_capacity];
    Bool built =
        build_member_path(path, sizeof(path), outside_directory_path, member);
    if (!built) {
      return {};
    }

    return Filesystem::File::read(
        NullTerminated(path, sizeof(path)).get_view());
  }

  auto create_directory(const char* member) const -> Bool {
    char path[temporary_path_capacity];
    Bool built = build_member_path(path, sizeof(path), root_path, member);
    if (!built) {
      return False;
    }

    return native_create_directory(path);
  }

  auto create_link(const char* member, const char* target) -> Bool {
    char path[temporary_path_capacity];
    Bool built = build_member_path(path, sizeof(path), root_path, member);
    if (!built) {
      return False;
    }

    return native_create_link(path, target, &link_error);
  }

  auto remove_root() -> Bool { return native_remove_tree(root_path); }

  auto rename_root() -> Bool {
    S32 written =
        snprintf(moved_path, sizeof(moved_path), "%s_moved", root_path);
    if (written <= 0 || CppSize(written) >= sizeof(moved_path)) {
      return False;
    }

    return native_rename(root_path, moved_path);
  }

  auto create_replacement_root() const -> Bool {
    return native_create_directory(root_path);
  }

  auto get_link_error() const -> S32 { return link_error; }

 private:
  S32 link_error = 0;
  char root_path[temporary_path_capacity]{};
  char moved_path[temporary_path_capacity]{};
  char outside_path[temporary_path_capacity]{};
  char outside_directory_path[temporary_path_capacity]{};
  Bool valid = False;
};

// Independent filesystem operations arrange each fixture so assertions only
// measure Perimortem's behavior. Native filesystem operations provide the same
// fixture setup and teardown in Windows and Unix checkouts.
static char file_directory[temporary_path_capacity];
static char original_directory[temporary_path_capacity];
static Harness SystemFile = {
  .name = "System::Filesystem::File",
  .setup =
      [] {
        if (!native_current_directory(
                original_directory, sizeof(original_directory)) ||
            !create_temporary_directory(
                file_directory, sizeof(file_directory))) {
          Diagnostics::Log::fatal(
              "Unable to create the file test directory."_view);
        }

        if (!native_change_directory(file_directory)) {
          Diagnostics::Log::fatal(
              "Unable to enter the file test directory."_view);
        }

        Filesystem::File::write(test_contents, test_file);
      },

  .teardown =
      [] {
        if (!native_change_directory(original_directory)) {
          Diagnostics::Log::fatal("Unable to restore the test directory."_view);
        }

        cleanup_temporary_root(file_directory);
      },
};

VALIDATION_TEST(SystemFile, read) {
  auto source = Filesystem::File::read(test_file);
  ASSERT(source);
  expect_contents(source, test_contents, result);
}

VALIDATION_TEST(SystemFile, arena_read) {
  Allocator::Arena arena;
  Bool written = Filesystem::File::write(test_contents, test_output);
  ASSERT(written);

  auto source = Filesystem::File::read(arena, test_output);
  ASSERT(source);

  Bool replaced = Filesystem::File::write(replacement_contents, test_output);
  ASSERT(replaced);

  auto replacement = Filesystem::File::read(arena, test_output);
  ASSERT(replacement);

  Bool emptied = Filesystem::File::write(View::Bytes(), test_output);
  ASSERT(emptied);

  auto empty = Filesystem::File::read(arena, test_output);
  ASSERT(empty);

  expect_contents(source, test_contents, result);
  expect_contents(replacement, replacement_contents, result);
  expect_selected(
      empty, [&](View::Bytes contents) { EXPECT(contents.is_empty()); },
      result);
}

VALIDATION_TEST(SystemFile, write) {
  Bool written = Filesystem::File::write(test_contents, test_output);
  ASSERT(written);

  auto source = Filesystem::File::read(test_output);
  ASSERT(source);
  expect_contents(source, test_contents, result);
}

VALIDATION_TEST(SystemFile, empty) {
  Bool written = Filesystem::File::write(View::Bytes(), test_output);
  ASSERT(written);

  auto source = Filesystem::File::read(test_output);
  ASSERT(source);
  expect_selected(
      source,
      [&](const Global::Bytes& contents) { EXPECT(contents.is_empty()); },
      result);
}

VALIDATION_TEST(SystemFile, missing) {
  Filesystem::File::remove(test_output);

  capture_next_file_log();

  auto source = Filesystem::File::read(test_output);
  EXPECT_NOT(source);
  EXPECT(file_warning_contains(
      "System::Filesystem::File read failed. "
      "path=system_file_test_out.json "
      "stage=open errno="_view));

  Bool removed = Filesystem::File::remove(test_output);
  EXPECT_NOT(removed);
}

VALIDATION_TEST(SystemFile, write_warning) {
  capture_next_file_log();

  Bool written = Filesystem::File::write(test_contents, "."_view);
  EXPECT_NOT(written);
  EXPECT(file_warning_contains(
      "System::Filesystem::File write failed. path=. "
      "stage=open errno="_view));
}

VALIDATION_TEST(SystemFile, unreadable) {
  Bool written = Filesystem::File::write(test_contents, test_output);
  ASSERT(written);

#ifdef TOOLCHAIN_WINDOWS

  HANDLE held = CreateFileA(
      test_output_path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  ASSERT(held != INVALID_HANDLE_VALUE);

  auto source = Filesystem::File::read(test_output);
  CloseHandle(held);
  EXPECT_NOT(source);

#else

  S32 restricted = chmod(test_output_path, 0);
  ASSERT_EQ(restricted, 0);

  auto source = Filesystem::File::read(test_output);

  S32 restored = chmod(test_output_path, S_IRUSR | S_IWUSR);
  EXPECT_EQ(restored, 0);
  EXPECT_NOT(source);

#endif
}

VALIDATION_TEST(SystemFile, oversized) {

#ifdef TOOLCHAIN_WINDOWS

  HANDLE file = CreateFileA(
      test_output_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
  ASSERT(file != INVALID_HANDLE_VALUE);

  DWORD returned;
  const bool sparse = DeviceIoControl(
      file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr);
  if (!sparse) {
    const auto error = GetLastError();
    CloseHandle(file);
    if (error == ERROR_INVALID_FUNCTION || error == ERROR_NOT_SUPPORTED) {
      SKIP("Temporary filesystem does not support sparse files."_view);
    }

    ASSERT(sparse);
  }

  LARGE_INTEGER size;
  size.QuadPart = (S64(1) << 36) + 1;

  const bool sized =
      SetFilePointerEx(file, size, nullptr, FILE_BEGIN) && SetEndOfFile(file);
  CloseHandle(file);
  ASSERT(sized);

#else

  S32 file =
      open(test_output_path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  ASSERT(file >= 0);

  constexpr off_t oversized_file = (off_t(1) << 36) + 1;
  S32 truncated = ftruncate(file, oversized_file);
  S32 closed = close(file);
  ASSERT_EQ(truncated, 0);
  ASSERT_EQ(closed, 0);

#endif

  EXPECT_NOT(Filesystem::File::read(test_output));
}

VALIDATION_TEST(SystemFile, non_regular) {
  EXPECT_NOT(Filesystem::File::read("."_view));
}

#ifdef TOOLCHAIN_LINUX

VALIDATION_TEST(SystemFile, short_read) {
  Bool written = Filesystem::File::write(test_contents, test_output);
  ASSERT(written);

  EXPECT(trace_read(
      test_output_path, View::Bytes(), False,
      TraceMutation::TruncateAfterMetadata));

  written = Filesystem::File::write(test_contents, test_output);
  ASSERT(written);
  EXPECT(trace_read(
      test_output_path, View::Bytes(), False,
      TraceMutation::TruncateAfterMetadata, nullptr, nullptr, nullptr, True));
}

#endif

#ifdef TOOLCHAIN_LINUX

VALIDATION_TEST(SystemFile, alias_file) {
  Bool original_written = Filesystem::File::write(test_contents, test_output);
  ASSERT(original_written);

  Bool replacement_written =
      Filesystem::File::write(replacement_contents, test_replacement);
  ASSERT(replacement_written);

  EXPECT(trace_read(
      test_output_path, test_contents, True, TraceMutation::ReplacePath,
      test_replacement_path));

  original_written = Filesystem::File::write(test_contents, test_output);
  replacement_written =
      Filesystem::File::write(replacement_contents, test_replacement);
  ASSERT(original_written);
  ASSERT(replacement_written);
  EXPECT(trace_read(
      test_output_path, test_contents, True, TraceMutation::ReplacePath,
      test_replacement_path, nullptr, nullptr, True));
}

#endif

VALIDATION_TEST(SystemFile, exists) {
  EXPECT(Filesystem::File::exists(test_file));
  EXPECT_NOT(Filesystem::File::exists("perimortem/"_view));
  EXPECT_NOT(Filesystem::File::exists("perimortem"_view));
}

VALIDATION_TEST(SystemFile, remove) {
  Bool written = Filesystem::File::write(test_contents, test_output);
  ASSERT(written);

  Bool removed = Filesystem::File::remove(test_output);
  ASSERT(removed);
  EXPECT_NOT(Filesystem::File::exists(test_output));
}

static Harness SystemFileRoot = {
  .name = "System::Filesystem::File::Root",
};

VALIDATION_TEST(SystemFileRoot, open_directory) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  auto location = temporary.get_location();
  ASSERT(location.get_size() < temporary_path_capacity);

  View::Bytes terminated_location(
      temporary.get_path(), location.get_size() + 1);
  auto root = Filesystem::File::Root::open(terminated_location);
  EXPECT(root);
}

VALIDATION_TEST(SystemFileRoot, missing_root) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool removed = temporary.remove_root();
  ASSERT(removed);

  auto root = Filesystem::File::Root::open(temporary.get_location());
  EXPECT_NOT(root);
}

VALIDATION_TEST(SystemFileRoot, file_root) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool written = temporary.write_member("file", test_contents);
  ASSERT(written);

  char path[temporary_path_capacity];
  Bool built = temporary.get_member_path("file", path, sizeof(path));
  ASSERT(built);

  auto root = Filesystem::File::Root::open(
      NullTerminated(path, sizeof(path)).get_view());
  EXPECT_NOT(root);
}

VALIDATION_TEST(SystemFileRoot, relative_file) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool written = temporary.write_member("file", test_contents);
  ASSERT(written);

  auto root = Filesystem::File::Root::open(temporary.get_location());
  ASSERT(root);

  root.visit(
      [&](Filesystem::File::Root& selected_root) {
        auto source = selected_root.read("file"_view);
        ASSERT(source);
        expect_contents(source, test_contents, result);
      },

      [&] { EXPECT(false); });
}

VALIDATION_TEST(SystemFileRoot, arena_read) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool written = temporary.write_member("file", test_contents);
  ASSERT(written);

  auto root = Filesystem::File::Root::open(temporary.get_location());
  ASSERT(root);

  root.visit(
      [&](Filesystem::File::Root& selected_root) {
        Allocator::Arena arena;
        auto source = selected_root.read(arena, "file"_view);
        ASSERT(source);

        Bool replaced = selected_root.write(replacement_contents, "file"_view);
        ASSERT(replaced);

        auto replacement = selected_root.read(arena, "file"_view);
        ASSERT(replacement);

        Bool empty_written = selected_root.write(View::Bytes(), "empty"_view);
        ASSERT(empty_written);

        auto empty = selected_root.read(arena, "empty"_view);
        ASSERT(empty);

        expect_contents(source, test_contents, result);
        expect_contents(replacement, replacement_contents, result);
        expect_selected(
            empty, [&](View::Bytes contents) { EXPECT(contents.is_empty()); },
            result);
      },

      [&] { EXPECT(false); });
}

VALIDATION_TEST(SystemFileRoot, relative_write) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool directory_created = temporary.create_directory("inside");
  ASSERT(directory_created);

  auto root = Filesystem::File::Root::open(temporary.get_location());
  ASSERT(root);

  root.visit(
      [&](Filesystem::File::Root& selected_root) {
        Bool written = selected_root.write(test_contents, "written"_view);
        Bool nested_written =
            selected_root.write(test_contents, "inside/file"_view);
        ASSERT(written);
        ASSERT(nested_written);

        auto source = selected_root.read("written"_view);
        auto nested_source = selected_root.read("inside/file"_view);
        ASSERT(source);
        ASSERT(nested_source);
        expect_contents(source, test_contents, result);
        expect_contents(nested_source, test_contents, result);

        Bool replaced =
            selected_root.write(replacement_contents, "written"_view);
        Bool empty_written = selected_root.write(View::Bytes(), "empty"_view);
        ASSERT(replaced);
        ASSERT(empty_written);

        source = selected_root.read("written"_view);

        auto empty_source = selected_root.read("empty"_view);
        ASSERT(source);
        ASSERT(empty_source);
        expect_contents(source, replacement_contents, result);
        expect_selected(
            empty_source,
            [&](const Global::Bytes& contents) { EXPECT(contents.is_empty()); },
            result);
      },

      [&] { EXPECT(false); });
}

VALIDATION_TEST(SystemFileRoot, relative_remove) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool directory_created = temporary.create_directory("inside");
  Bool written = temporary.write_member("file", test_contents);
  Bool nested_written = temporary.write_member("inside/file", test_contents);
  ASSERT(directory_created);
  ASSERT(written);
  ASSERT(nested_written);

  auto root = Filesystem::File::Root::open(temporary.get_location());
  ASSERT(root);
  root.visit(
      [&](Filesystem::File::Root& selected_root) {
        ASSERT(selected_root.exists("file"_view));
        ASSERT(selected_root.exists("inside/file"_view));

        Bool removed = selected_root.remove("file"_view);
        Bool nested_removed = selected_root.remove("inside/file"_view);
        EXPECT(removed);
        EXPECT(nested_removed);
        EXPECT_NOT(selected_root.exists("file"_view));
        EXPECT_NOT(selected_root.exists("inside/file"_view));
      },

      [&] { EXPECT(false); });
}

VALIDATION_TEST(SystemFileRoot, relative_exists) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool written = temporary.write_member("file", test_contents);
  Bool directory_created = temporary.create_directory("directory");
  ASSERT(written);
  ASSERT(directory_created);

  auto root = Filesystem::File::Root::open(temporary.get_location());
  ASSERT(root);

  root.visit(
      [&](Filesystem::File::Root& selected_root) {
        EXPECT(selected_root.exists("file"_view));
        EXPECT_NOT(selected_root.exists("missing"_view));
        EXPECT_NOT(selected_root.exists("directory"_view));
      },

      [&] { EXPECT(false); });
}

VALIDATION_TEST(SystemFileRoot, empty_file) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool written = temporary.write_member("empty", View::Bytes());
  ASSERT(written);

  auto root = Filesystem::File::Root::open(temporary.get_location());
  ASSERT(root);

  root.visit(
      [&](Filesystem::File::Root& selected_root) {
        auto source = selected_root.read("empty"_view);
        ASSERT(source);
        expect_selected(
            source,
            [&](const Global::Bytes& contents) { EXPECT(contents.is_empty()); },
            result);
      },

      [&] { EXPECT(false); });
}

VALIDATION_TEST(SystemFileRoot, missing_member) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  auto root = Filesystem::File::Root::open(temporary.get_location());
  ASSERT(root);

  capture_next_file_log();

  root.visit(
      [&](Filesystem::File::Root& selected_root) {
        EXPECT_NOT(selected_root.read("missing"_view));
      },

      [&] { EXPECT(false); });
  EXPECT_EQ(captured_file_log_size, Count(0));
}

VALIDATION_TEST(SystemFileRoot, directory_member) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool created = temporary.create_directory("directory");
  ASSERT(created);

  auto root = Filesystem::File::Root::open(temporary.get_location());
  ASSERT(root);

  root.visit(
      [&](Filesystem::File::Root& selected_root) {
        EXPECT_NOT(selected_root.read("directory"_view));
      },

      [&] { EXPECT(false); });
}

VALIDATION_TEST(SystemFileRoot, absolute_member) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool written = temporary.write_member("file", test_contents);
  ASSERT(written);

  char path[temporary_path_capacity];
  Bool built = temporary.get_member_path("file", path, sizeof(path));
  ASSERT(built);

  auto root = Filesystem::File::Root::open(temporary.get_location());
  ASSERT(root);

  root.visit(
      [&](Filesystem::File::Root& selected_root) {
        auto source =
            selected_root.read(NullTerminated(path, sizeof(path)).get_view());
        ASSERT(source);
        expect_contents(source, test_contents, result);
      },

      [&] { EXPECT(false); });
}

VALIDATION_TEST(SystemFileRoot, safe_normalization) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool directory_created = temporary.create_directory("inside");
  Bool written = temporary.write_member("file", test_contents);
  ASSERT(directory_created);
  ASSERT(written);

  auto root = Filesystem::File::Root::open(temporary.get_location());
  ASSERT(root);

  root.visit(
      [&](Filesystem::File::Root& selected_root) {
        auto source = selected_root.read("inside/../file"_view);
        ASSERT(source);
        expect_contents(source, test_contents, result);
      },

      [&] { EXPECT(false); });
}

VALIDATION_TEST(SystemFileRoot, symlink_read) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool outside_written = temporary.write_outside(test_contents);
  Bool linked = temporary.create_link("link", temporary.get_outside_path());
  ASSERT(outside_written);

#ifdef TOOLCHAIN_WINDOWS

  if (!linked && temporary.get_link_error() == ERROR_PRIVILEGE_NOT_HELD) {
    SKIP(
        "Symbolic links require Windows Developer Mode or link privileges."_view);
  }

#endif

  ASSERT(linked);

  auto root = Filesystem::File::Root::open(temporary.get_location());
  ASSERT(root);

  root.visit(
      [&](Filesystem::File::Root& selected_root) {
        auto source = selected_root.read("link"_view);
        ASSERT(source);
        expect_contents(source, test_contents, result);
      },

      [&] { EXPECT(false); });
}

VALIDATION_TEST(SystemFileRoot, symlink_write) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool outside_written = temporary.write_outside(test_contents);
  Bool linked = temporary.create_link("link", temporary.get_outside_path());
  ASSERT(outside_written);

#ifdef TOOLCHAIN_WINDOWS

  if (!linked && temporary.get_link_error() == ERROR_PRIVILEGE_NOT_HELD) {
    SKIP(
        "Symbolic links require Windows Developer Mode or link privileges."_view);
  }

#endif

  ASSERT(linked);

  auto root = Filesystem::File::Root::open(temporary.get_location());
  ASSERT(root);

  root.visit(
      [&](Filesystem::File::Root& selected_root) {
        EXPECT(selected_root.write(replacement_contents, "link"_view));
      },

      [&] { EXPECT(false); });

  auto outside_source = Filesystem::File::read(
      NullTerminated(temporary.get_outside_path(), temporary_path_capacity)
          .get_view());
  ASSERT(outside_source);
  expect_contents(outside_source, replacement_contents, result);
}

VALIDATION_TEST(SystemFileRoot, symlink_remove) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool directory_created = temporary.create_outside_directory();
  Bool outside_written = temporary.write_outside_member("file", test_contents);
  Bool linked =
      temporary.create_link("link", temporary.get_outside_directory_path());
  ASSERT(directory_created);
  ASSERT(outside_written);

#ifdef TOOLCHAIN_WINDOWS

  if (!linked && temporary.get_link_error() == ERROR_PRIVILEGE_NOT_HELD) {
    SKIP(
        "Symbolic links require Windows Developer Mode or link privileges."_view);
  }

#endif

  ASSERT(linked);

  auto root = Filesystem::File::Root::open(temporary.get_location());
  ASSERT(root);

  root.visit(
      [&](Filesystem::File::Root& selected_root) {
        EXPECT(selected_root.remove("link/file"_view));
      },

      [&] { EXPECT(false); });

  EXPECT_NOT(temporary.read_outside_member("file"));
}

VALIDATION_TEST(SystemFileRoot, symlink_exists) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool outside_written = temporary.write_outside(test_contents);
  Bool linked = temporary.create_link("link", temporary.get_outside_path());
  ASSERT(outside_written);

#ifdef TOOLCHAIN_WINDOWS

  if (!linked && temporary.get_link_error() == ERROR_PRIVILEGE_NOT_HELD) {
    SKIP(
        "Symbolic links require Windows Developer Mode or link privileges."_view);
  }

#endif

  ASSERT(linked);

  auto root = Filesystem::File::Root::open(temporary.get_location());
  ASSERT(root);
  root.visit(
      [&](Filesystem::File::Root& selected_root) {
        EXPECT(selected_root.exists("link"_view));
      },

      [&] { EXPECT(false); });
}

#ifdef TOOLCHAIN_LINUX

VALIDATION_TEST(SystemFileRoot, write_close_warning) {
  auto root = Filesystem::File::Root::open("/dev"_view);
  ASSERT(root);

  capture_next_file_log();

  root.visit(
      [&](Filesystem::File::Root& selected_root) {
        EXPECT_NOT(selected_root.write(test_contents, "full"_view));
      },

      [&] { EXPECT(false); });
  EXPECT(file_warning_contains(
      "System::Filesystem::File write failed. path=/dev/full "
      "stage=close errno="_view));
}

#endif

VALIDATION_TEST(SystemFileRoot, directory_replacement) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool original_written = temporary.write_member("file", test_contents);
  ASSERT(original_written);

  auto root = Filesystem::File::Root::open(temporary.get_location());
  ASSERT(root);

  Bool moved = temporary.rename_root();
  Bool replacement_created = temporary.create_replacement_root();
  Bool replacement_written =
      temporary.write_member("file", replacement_contents);
  ASSERT(moved);
  ASSERT(replacement_created);
  ASSERT(replacement_written);

  root.visit(
      [&](Filesystem::File::Root& selected_root) {
        auto source = selected_root.read("file"_view);
        ASSERT(source);
        expect_contents(source, replacement_contents, result);
      },

      [&] { EXPECT(false); });
}

#ifdef TOOLCHAIN_LINUX

VALIDATION_TEST(SystemFileRoot, alias_member) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool original_written = temporary.write_member("file", test_contents);
  Bool replacement_written =
      temporary.write_member("replacement", replacement_contents);
  ASSERT(original_written);
  ASSERT(replacement_written);

  char path[temporary_path_capacity];
  char replacement[temporary_path_capacity];
  Bool path_built = temporary.get_member_path("file", path, sizeof(path));
  Bool replacement_built = temporary.get_member_path(
      "replacement", replacement, sizeof(replacement));
  ASSERT(path_built);
  ASSERT(replacement_built);

  EXPECT(trace_read(
      path, test_contents, True, TraceMutation::ReplacePath, replacement,
      temporary.get_path(), "file"));

  original_written = temporary.write_member("file", test_contents);
  replacement_written =
      temporary.write_member("replacement", replacement_contents);
  ASSERT(original_written);
  ASSERT(replacement_written);
  EXPECT(trace_read(
      path, test_contents, True, TraceMutation::ReplacePath, replacement,
      temporary.get_path(), "file", True));
}

#endif

VALIDATION_TEST(SystemFileRoot, move_ownership) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool written = temporary.write_member("file", test_contents);
  ASSERT(written);

  {
    auto first_source = Filesystem::File::Root::open(temporary.get_location());
    auto second_source = Filesystem::File::Root::open(temporary.get_location());
    ASSERT(first_source);
    ASSERT(second_source);

    first_source.visit(
        [&](Filesystem::File::Root& first_value) {
          second_source.visit(
              [&](Filesystem::File::Root& second_value) {
                Filesystem::File::Root first(Data::take(first_value));
                Filesystem::File::Root moved(Data::take(first));
                Filesystem::File::Root assigned(Data::take(second_value));
                assigned = Data::take(moved);

                auto source = assigned.read("file"_view);
                ASSERT(source);
                expect_contents(source, test_contents, result);
              },

              [&] { EXPECT(false); });
        },

        [&] { EXPECT(false); });
  }
}

VALIDATION_TEST(SystemFileRoot, invalid_routes) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool written = temporary.write_member("file", test_contents);
  ASSERT(written);

  auto root = Filesystem::File::Root::open(temporary.get_location());
  ASSERT(root);

  root.visit(
      [&](Filesystem::File::Root& selected_root) {
        U8 embedded_null[] = {'f', 'i', 'l', 'e', '\0', 'x'};
        auto embedded_source = selected_root.read(
            View::Bytes(embedded_null, sizeof(embedded_null)));
        EXPECT_NOT(embedded_source);

        Storage::Bytes<5> terminated_path = {{'f', 'i', 'l', 'e', '\0'}};
        auto terminated_source = selected_root.read(terminated_path);
        ASSERT(terminated_source);
        expect_contents(terminated_source, test_contents, result);

        Storage::Bytes<Filesystem::Path::max_size + 1> overlong;
        for (Count i = 0; i < overlong.get_size(); i++) {
          overlong[i] = 'a';
        }

        auto overlong_source = selected_root.read(overlong);
        EXPECT_NOT(overlong_source);
      },

      [&] { EXPECT(false); });
}

// Root is a directory location, so a parent segment can reach a sibling. Keep
// this explicit because lexical normalization used to reject that operation.
VALIDATION_TEST(SystemFileRoot, parent_member) {
  TemporaryRoot temporary;
  ASSERT(temporary);
  ASSERT(temporary.write_outside(test_contents));

  auto root = Filesystem::File::Root::open(temporary.get_location());
  ASSERT(root);

  char relative[temporary_path_capacity];
  const char* name = strrchr(temporary.get_outside_path(), '/');
  ASSERT(name);

  S32 written = snprintf(relative, sizeof(relative), "../%s", name + 1);
  ASSERT(written >= 0);

  Count size = written;
  ASSERT(size < sizeof(relative));

  root.visit(
      [&](Filesystem::File::Root& selected_root) {
        auto source = selected_root.read(View::Bytes(relative, size));
        ASSERT(source);
        expect_contents(source, test_contents, result);
      },

      [&] { EXPECT(false); });
}

// A Snapshot's fingerprint and bytes come from the same open object. A file
// replacement at that location produces distinct content and a distinct
// fingerprint.
VALIDATION_TEST(SystemFileRoot, snapshot_identity) {
  TemporaryRoot temporary;
  ASSERT(temporary);
  ASSERT(temporary.write_member("file", test_contents));
  ASSERT(temporary.write_member("replacement", replacement_contents));

  auto root = Filesystem::File::Root::open(temporary.get_location());
  ASSERT(root);

  root.visit(
      [&](Filesystem::File::Root& selected_root) {
        auto before = selected_root.read_snapshot("file"_view);
        ASSERT(before);

        auto fingerprint = selected_root.fingerprint("file"_view);
        ASSERT(fingerprint);
        before.visit(
            [&](const Filesystem::File::Snapshot& snapshot) {
              fingerprint.visit(
                  [&](const Filesystem::File::Fingerprint&
                          selected_fingerprint) {
                    EXPECT(snapshot.get_fingerprint() == selected_fingerprint);
                  },

                  [&] { EXPECT(false); });
            },

            [&] { EXPECT(false); });

        char source[temporary_path_capacity];
        char destination[temporary_path_capacity];
        ASSERT(
            temporary.get_member_path("replacement", source, sizeof(source)));
        ASSERT(temporary.get_member_path(
            "file", destination, sizeof(destination)));
        ASSERT(
            Filesystem::File::replace(
                NullTerminated(source, sizeof(source)).get_view(),
                NullTerminated(destination, sizeof(destination)).get_view()));

        auto after = selected_root.read_snapshot("file"_view);
        ASSERT(after);
        before.visit(
            [&](const Filesystem::File::Snapshot& before_snapshot) {
              after.visit(
                  [&](const Filesystem::File::Snapshot& after_snapshot) {
                    EXPECT_TEXT(before_snapshot.get_contents(), test_contents);
                    EXPECT_TEXT(
                        after_snapshot.get_contents(), replacement_contents);
                    EXPECT(
                        !(before_snapshot.get_fingerprint() ==
                          after_snapshot.get_fingerprint()));
                  },

                  [&] { EXPECT(false); });
            },

            [&] { EXPECT(false); });
      },

      [&] { EXPECT(false); });
}
