// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/diagnostics/log.hpp"

#include "perimortem/system/file.hpp"

#include "toolchain/validation/unit_test.hpp"

#include <filesystem>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PERI_LINUX
#include <fcntl.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>
#elifdef PERI_WINDOWS
#include <windows.h>
#include <winioctl.h>
#endif

#include "perimortem/core/algorithm/search.hpp"
#include "perimortem/core/null_terminated.hpp"

#include "perimortem/memory/allocator/arena.hpp"

#include "perimortem/system/path.hpp"
#include "perimortem/system/random.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;
using namespace Toolchain::Validation;

constexpr auto test_file = "system_file_test.json"_view;
constexpr auto test_output = "system_file_test_out.json"_view;
constexpr auto test_replacement = "system_file_test_replacement.json"_view;
constexpr auto test_contents = "{\"value\":42}"_view;
constexpr auto replacement_contents = "{\"other\":24}"_view;

static constexpr const char* test_output_path = "system_file_test_out.json";

#ifdef PERI_LINUX

static constexpr const char* test_replacement_path =
    "system_file_test_replacement.json";

#endif

static constexpr CppSize temporary_path_capacity = 256;
static constexpr Count captured_file_log_capacity = 1024;

static Diagnostics::Log::Level captured_file_log_level;
static Static::Bytes<captured_file_log_capacity> captured_file_log;
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
  return Algorithm::search(captured, message) != Count(-1);
}

// Linux syscall tracing provides deterministic mutation between open, metadata,
// and read. Keep that additional kernel oracle separate from the portable file
// behavior exercised on both hosts.

#ifdef PERI_LINUX

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
  Option<File::Root> root;
  if (root_path) {
    root = File::Root::open(
        NullTerminated::convert_cstring(root_path, temporary_path_capacity));
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
      NullTerminated::convert_cstring(selected_path, temporary_path_capacity);
  if (arena_read) {
    Allocator::Arena arena;
    auto source = root ? (*root).read(arena, requested_path)
                       : File::read(arena, requested_path);
    if (!expected_present) {
      _exit(source ? 1 : 0);
    }

    _exit(source && *source == expected ? 0 : 1);
  }

  auto source =
      root ? (*root).read(requested_path) : File::read(requested_path);
  if (!expected_present) {
    _exit(source ? 1 : 0);
  }

  _exit(source && *source == expected ? 0 : 1);
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

  long option_result =
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
    long trace_result = ptrace(PTRACE_SYSCALL, child, nullptr, nullptr);
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
    long registers_result = ptrace(PTRACE_GETREGS, child, nullptr, &registers);
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

static auto native_path(const char* path) -> std::filesystem::path {
  std::u8string text;
  while (*path) {
    text.push_back(char8_t(*path++));
  }
  return std::filesystem::path(text);
}

static auto create_temporary_directory(char* output, CppSize capacity) -> Bool {
  std::error_code error;
  auto path = std::filesystem::temp_directory_path(error);
  if (error) {
    return False;
  }
  char name[64];
  snprintf(name, sizeof(name), "perimortem_file_%llx", Random::generate());
  path /= name;
  const auto text = path.generic_u8string();
  if (text.size() >= capacity) {
    return False;
  }
  if (!std::filesystem::create_directory(path, error)) {
    return False;
  }
  memcpy(output, text.c_str(), text.size() + 1);
  return True;
}

static auto cleanup_temporary_root(const char* root) -> void {
  if (!root[0]) {
    return;
  }
  std::error_code error;
  std::filesystem::remove_all(native_path(root), error);
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
    return NullTerminated::convert_cstring(root_path, sizeof(root_path));
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

    return File::write(
        data, NullTerminated::convert_cstring(path, sizeof(path)));
  }

  auto write_outside(View::Bytes data) const -> Bool {
    return File::write(
        data, NullTerminated::convert_cstring(outside_path, sizeof(outside_path)));
  }

  auto create_outside_directory() const -> Bool {
    std::error_code error;
    return std::filesystem::create_directory(
        native_path(outside_directory_path), error);
  }

  auto write_outside_member(const char* member, View::Bytes data) const
      -> Bool {
    char path[temporary_path_capacity];
    Bool built =
        build_member_path(path, sizeof(path), outside_directory_path, member);
    if (!built) {
      return False;
    }

    return File::write(
        data, NullTerminated::convert_cstring(path, sizeof(path)));
  }

  auto read_outside_member(const char* member) const
      -> Option<Perimortem::Memory::Dynamic::Bytes> {
    char path[temporary_path_capacity];
    Bool built =
        build_member_path(path, sizeof(path), outside_directory_path, member);
    if (!built) {
      return {};
    }

    return File::read(NullTerminated::convert_cstring(path, sizeof(path)));
  }

  auto create_directory(const char* member) const -> Bool {
    char path[temporary_path_capacity];
    Bool built = build_member_path(path, sizeof(path), root_path, member);
    if (!built) {
      return False;
    }

    std::error_code error;
    return std::filesystem::create_directory(native_path(path), error);
  }

  auto create_link(const char* member, const char* target) -> Bool {
    char path[temporary_path_capacity];
    Bool built = build_member_path(path, sizeof(path), root_path, member);
    if (!built) {
      return False;
    }

    std::error_code error;
    const auto destination = native_path(target);
    if (std::filesystem::is_directory(destination, error)) {
      std::filesystem::create_directory_symlink(
          destination, native_path(path), error);
    } else {
      std::filesystem::create_symlink(destination, native_path(path), error);
    }
    link_error = error.value();
    return !error;
  }

  auto remove_root() -> Bool {
    std::error_code error;
    return std::filesystem::remove(native_path(root_path), error);
  }

  auto rename_root() -> Bool {
    S32 written =
        snprintf(moved_path, sizeof(moved_path), "%s_moved", root_path);
    if (written <= 0 || CppSize(written) >= sizeof(moved_path)) {
      return False;
    }

    std::error_code error;
    std::filesystem::rename(
        native_path(root_path), native_path(moved_path), error);
    return !error;
  }

  auto create_replacement_root() const -> Bool {
    std::error_code error;
    return std::filesystem::create_directory(native_path(root_path), error);
  }

  auto get_link_error() const -> int { return link_error; }

 private:
  int link_error = 0;
  char root_path[temporary_path_capacity]{};
  char moved_path[temporary_path_capacity]{};
  char outside_path[temporary_path_capacity]{};
  char outside_directory_path[temporary_path_capacity]{};
  Bool valid = False;
};

// Independent filesystem operations arrange each fixture so assertions only
// measure Perimortem's behavior. This also works in a native Windows checkout
// without requiring a Unix temporary directory or directory descriptors.
static char file_directory[temporary_path_capacity];
static std::filesystem::path original_directory;
static Harness SystemFile = {
  .name = "System::File",
  .setup =
      [] {
        std::error_code error;
        original_directory = std::filesystem::current_path(error);
        if (error || !create_temporary_directory(
                         file_directory, sizeof(file_directory))) {
          Diagnostics::Log::fatal(
              "Unable to create the file test directory."_view);
        }
        std::filesystem::current_path(native_path(file_directory), error);
        if (error) {
          Diagnostics::Log::fatal(
              "Unable to enter the file test directory."_view);
        }
        File::write(test_contents, test_file);
      },
  .teardown =
      [] {
        std::error_code error;
        std::filesystem::current_path(original_directory, error);
        if (error) {
          Diagnostics::Log::fatal("Unable to restore the test directory."_view);
        }
        cleanup_temporary_root(file_directory);
      },
};

VALIDATION_TEST(SystemFile, read) {
  auto source = File::read(test_file);
  ASSERT(source);
  EXPECT_TEXT(*source, test_contents);
}

VALIDATION_TEST(SystemFile, arena_read) {
  Allocator::Arena arena;
  Bool written = File::write(test_contents, test_output);
  ASSERT(written);

  auto source = File::read(arena, test_output);
  ASSERT(source);

  Bool replaced = File::write(replacement_contents, test_output);
  ASSERT(replaced);
  auto replacement = File::read(arena, test_output);
  ASSERT(replacement);

  Bool emptied = File::write(View::Bytes(), test_output);
  ASSERT(emptied);
  auto empty = File::read(arena, test_output);
  ASSERT(empty);

  EXPECT_TEXT(*source, test_contents);
  EXPECT_TEXT(*replacement, replacement_contents);
  EXPECT((*empty).is_empty());
}

VALIDATION_TEST(SystemFile, write) {
  Bool written = File::write(test_contents, test_output);
  ASSERT(written);

  auto source = File::read(test_output);
  ASSERT(source);
  EXPECT_TEXT(*source, test_contents);
}

VALIDATION_TEST(SystemFile, empty) {
  Bool written = File::write(View::Bytes(), test_output);
  ASSERT(written);

  auto source = File::read(test_output);
  ASSERT(source);
  EXPECT((*source).is_empty());
}

VALIDATION_TEST(SystemFile, missing) {
  File::remove(test_output);

  capture_next_file_log();
  auto source = File::read(test_output);
  EXPECT_NOT(source);
  EXPECT(file_warning_contains(
      "System::File read failed. "
      "path=system_file_test_out.json "
      "stage=open errno="_view));

  Bool removed = File::remove(test_output);
  EXPECT_NOT(removed);
}

VALIDATION_TEST(SystemFile, write_warning) {
  capture_next_file_log();
  Bool written = File::write(test_contents, "."_view);
  EXPECT_NOT(written);
  EXPECT(file_warning_contains(
      "System::File write failed. path=. "
      "stage=open errno="_view));
}

VALIDATION_TEST(SystemFile, unreadable) {
  Bool written = File::write(test_contents, test_output);
  ASSERT(written);

#ifdef PERI_WINDOWS

  HANDLE held = CreateFileA(
      test_output_path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  ASSERT(held != INVALID_HANDLE_VALUE);
  auto source = File::read(test_output);
  CloseHandle(held);
  EXPECT_NOT(source);

#else

  S32 restricted = chmod(test_output_path, 0);
  ASSERT_EQ(restricted, 0);

  auto source = File::read(test_output);

  S32 restored = chmod(test_output_path, S_IRUSR | S_IWUSR);
  EXPECT_EQ(restored, 0);
  EXPECT_NOT(source);

#endif

}

VALIDATION_TEST(SystemFile, oversized) {

#ifdef PERI_WINDOWS

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
  size.QuadPart = (S64(1) << 35) + 1;
  const bool sized =
      SetFilePointerEx(file, size, nullptr, FILE_BEGIN) && SetEndOfFile(file);
  CloseHandle(file);
  ASSERT(sized);

#else

  S32 descriptor =
      open(test_output_path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  ASSERT(descriptor >= 0);

  constexpr off_t oversized_file = (off_t(1) << 35) + 1;
  S32 truncated = ftruncate(descriptor, oversized_file);
  S32 closed = close(descriptor);
  ASSERT_EQ(truncated, 0);
  ASSERT_EQ(closed, 0);

#endif

  EXPECT_NOT(File::read(test_output));
}

VALIDATION_TEST(SystemFile, non_regular) {
  EXPECT_NOT(File::read("."_view));
}

#ifdef PERI_LINUX

VALIDATION_TEST(SystemFile, short_read) {
  Bool written = File::write(test_contents, test_output);
  ASSERT(written);

  EXPECT(trace_read(
      test_output_path, View::Bytes(), False,
      TraceMutation::TruncateAfterMetadata));

  written = File::write(test_contents, test_output);
  ASSERT(written);
  EXPECT(trace_read(
      test_output_path, View::Bytes(), False,
      TraceMutation::TruncateAfterMetadata, nullptr, nullptr, nullptr, True));
}

#endif

#ifdef PERI_LINUX

VALIDATION_TEST(SystemFile, same_opened_object) {
  Bool original_written = File::write(test_contents, test_output);
  ASSERT(original_written);

  Bool replacement_written =
      File::write(replacement_contents, test_replacement);
  ASSERT(replacement_written);

  EXPECT(trace_read(
      test_output_path, test_contents, True, TraceMutation::ReplacePath,
      test_replacement_path));

  original_written = File::write(test_contents, test_output);
  replacement_written = File::write(replacement_contents, test_replacement);
  ASSERT(original_written);
  ASSERT(replacement_written);
  EXPECT(trace_read(
      test_output_path, test_contents, True, TraceMutation::ReplacePath,
      test_replacement_path, nullptr, nullptr, True));
}

#endif

VALIDATION_TEST(SystemFile, exists) {
  EXPECT(File::exists(test_file));
  EXPECT_NOT(File::exists("perimortem/"_view));
  EXPECT_NOT(File::exists("perimortem"_view));
}

VALIDATION_TEST(SystemFile, remove) {
  Bool written = File::write(test_contents, test_output);
  ASSERT(written);

  Bool removed = File::remove(test_output);
  ASSERT(removed);
  EXPECT_NOT(File::exists(test_output));
}

static Harness SystemFileRoot = {
  .name = "System::File::Root",
};

VALIDATION_TEST(SystemFileRoot, open_directory) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  auto location = temporary.get_location();
  ASSERT(location.get_size() < temporary_path_capacity);
  View::Bytes terminated_location =
      NullTerminated::to_view(temporary.get_path(), location.get_size() + 1);
  auto root = File::Root::open(terminated_location);
  EXPECT(root);
}

VALIDATION_TEST(SystemFileRoot, missing_root) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool removed = temporary.remove_root();
  ASSERT(removed);

  auto root = File::Root::open(temporary.get_location());
  EXPECT_NOT(root);
}

VALIDATION_TEST(SystemFileRoot, regular_file_root) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool written = temporary.write_member("file", test_contents);
  ASSERT(written);

  char path[temporary_path_capacity];
  Bool built = temporary.get_member_path("file", path, sizeof(path));
  ASSERT(built);

  auto root = File::Root::open(
      NullTerminated::convert_cstring(path, sizeof(path)));
  EXPECT_NOT(root);
}

VALIDATION_TEST(SystemFileRoot, relative_file) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool written = temporary.write_member("file", test_contents);
  ASSERT(written);

  auto root = File::Root::open(temporary.get_location());
  ASSERT(root);

  auto source = (*root).read("file"_view);
  ASSERT(source);
  EXPECT_TEXT(*source, test_contents);
}

VALIDATION_TEST(SystemFileRoot, arena_read) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool written = temporary.write_member("file", test_contents);
  ASSERT(written);

  auto root = File::Root::open(temporary.get_location());
  ASSERT(root);

  Allocator::Arena arena;
  auto source = (*root).read(arena, "file"_view);
  ASSERT(source);

  Bool replaced = (*root).write(replacement_contents, "file"_view);
  ASSERT(replaced);
  auto replacement = (*root).read(arena, "file"_view);
  ASSERT(replacement);

  Bool empty_written = (*root).write(View::Bytes(), "empty"_view);
  ASSERT(empty_written);
  auto empty = (*root).read(arena, "empty"_view);
  ASSERT(empty);

  EXPECT_TEXT(*source, test_contents);
  EXPECT_TEXT(*replacement, replacement_contents);
  EXPECT((*empty).is_empty());
}

VALIDATION_TEST(SystemFileRoot, relative_write) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool directory_created = temporary.create_directory("inside");
  ASSERT(directory_created);

  auto root = File::Root::open(temporary.get_location());
  ASSERT(root);

  Bool written = (*root).write(test_contents, "written"_view);
  Bool nested_written = (*root).write(test_contents, "inside/file"_view);
  ASSERT(written);
  ASSERT(nested_written);

  auto source = (*root).read("written"_view);
  auto nested_source = (*root).read("inside/file"_view);
  ASSERT(source);
  ASSERT(nested_source);
  EXPECT_TEXT(*source, test_contents);
  EXPECT_TEXT(*nested_source, test_contents);

  Bool replaced = (*root).write(replacement_contents, "written"_view);
  Bool empty_written = (*root).write(View::Bytes(), "empty"_view);
  ASSERT(replaced);
  ASSERT(empty_written);

  source = (*root).read("written"_view);
  auto empty_source = (*root).read("empty"_view);
  ASSERT(source);
  ASSERT(empty_source);
  EXPECT_TEXT(*source, replacement_contents);
  EXPECT((*empty_source).is_empty());
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

  auto root = File::Root::open(temporary.get_location());
  ASSERT(root);
  ASSERT((*root).exists("file"_view));
  ASSERT((*root).exists("inside/file"_view));

  Bool removed = (*root).remove("file"_view);
  Bool nested_removed = (*root).remove("inside/file"_view);
  EXPECT(removed);
  EXPECT(nested_removed);
  EXPECT_NOT((*root).exists("file"_view));
  EXPECT_NOT((*root).exists("inside/file"_view));
}

VALIDATION_TEST(SystemFileRoot, relative_exists) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool written = temporary.write_member("file", test_contents);
  Bool directory_created = temporary.create_directory("directory");
  ASSERT(written);
  ASSERT(directory_created);

  auto root = File::Root::open(temporary.get_location());
  ASSERT(root);

  EXPECT((*root).exists("file"_view));
  EXPECT_NOT((*root).exists("missing"_view));
  EXPECT_NOT((*root).exists("directory"_view));
}

VALIDATION_TEST(SystemFileRoot, empty_file) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool written = temporary.write_member("empty", View::Bytes());
  ASSERT(written);

  auto root = File::Root::open(temporary.get_location());
  ASSERT(root);

  auto source = (*root).read("empty"_view);
  ASSERT(source);
  EXPECT((*source).is_empty());
}

VALIDATION_TEST(SystemFileRoot, missing_member) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  auto root = File::Root::open(temporary.get_location());
  ASSERT(root);

  capture_next_file_log();
  auto source = (*root).read("missing"_view);
  EXPECT_NOT(source);
  EXPECT_EQ(captured_file_log_size, Count(0));
}

VALIDATION_TEST(SystemFileRoot, directory_member) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool created = temporary.create_directory("directory");
  ASSERT(created);

  auto root = File::Root::open(temporary.get_location());
  ASSERT(root);

  auto source = (*root).read("directory"_view);
  EXPECT_NOT(source);
}

VALIDATION_TEST(SystemFileRoot, absolute_member) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool written = temporary.write_member("file", test_contents);
  ASSERT(written);

  char path[temporary_path_capacity];
  Bool built = temporary.get_member_path("file", path, sizeof(path));
  ASSERT(built);

  auto root = File::Root::open(temporary.get_location());
  ASSERT(root);

  auto source = (*root).read(
      NullTerminated::convert_cstring(path, sizeof(path)));
  ASSERT(source);
  EXPECT_TEXT(*source, test_contents);
}

VALIDATION_TEST(SystemFileRoot, safe_normalization) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool directory_created = temporary.create_directory("inside");
  Bool written = temporary.write_member("file", test_contents);
  ASSERT(directory_created);
  ASSERT(written);

  auto root = File::Root::open(temporary.get_location());
  ASSERT(root);

  auto source = (*root).read("inside/../file"_view);
  ASSERT(source);
  EXPECT_TEXT(*source, test_contents);
}

VALIDATION_TEST(SystemFileRoot, symlink_read) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool outside_written = temporary.write_outside(test_contents);
  Bool linked = temporary.create_link("link", temporary.get_outside_path());
  ASSERT(outside_written);

#ifdef PERI_WINDOWS

  if (!linked && temporary.get_link_error() == ERROR_PRIVILEGE_NOT_HELD) {
    SKIP(
        "Symbolic links require Windows Developer Mode or link privileges."_view);
  }

#endif

  ASSERT(linked);

  auto root = File::Root::open(temporary.get_location());
  ASSERT(root);

  auto source = (*root).read("link"_view);
  ASSERT(source);
  EXPECT_TEXT(*source, test_contents);
}

VALIDATION_TEST(SystemFileRoot, symlink_write) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool outside_written = temporary.write_outside(test_contents);
  Bool linked = temporary.create_link("link", temporary.get_outside_path());
  ASSERT(outside_written);

#ifdef PERI_WINDOWS

  if (!linked && temporary.get_link_error() == ERROR_PRIVILEGE_NOT_HELD) {
    SKIP(
        "Symbolic links require Windows Developer Mode or link privileges."_view);
  }

#endif

  ASSERT(linked);

  auto root = File::Root::open(temporary.get_location());
  ASSERT(root);

  Bool written = (*root).write(replacement_contents, "link"_view);
  EXPECT(written);

  auto outside_source = File::read(NullTerminated::convert_cstring(
      temporary.get_outside_path(), temporary_path_capacity));
  ASSERT(outside_source);
  EXPECT_TEXT(*outside_source, replacement_contents);
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

#ifdef PERI_WINDOWS

  if (!linked && temporary.get_link_error() == ERROR_PRIVILEGE_NOT_HELD) {
    SKIP(
        "Symbolic links require Windows Developer Mode or link privileges."_view);
  }

#endif

  ASSERT(linked);

  auto root = File::Root::open(temporary.get_location());
  ASSERT(root);

  Bool removed = (*root).remove("link/file"_view);
  EXPECT(removed);

  EXPECT_NOT(temporary.read_outside_member("file"));
}

VALIDATION_TEST(SystemFileRoot, symlink_exists) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool outside_written = temporary.write_outside(test_contents);
  Bool linked = temporary.create_link("link", temporary.get_outside_path());
  ASSERT(outside_written);

#ifdef PERI_WINDOWS

  if (!linked && temporary.get_link_error() == ERROR_PRIVILEGE_NOT_HELD) {
    SKIP(
        "Symbolic links require Windows Developer Mode or link privileges."_view);
  }

#endif

  ASSERT(linked);

  auto root = File::Root::open(temporary.get_location());
  ASSERT(root);
  EXPECT((*root).exists("link"_view));
}

#ifdef PERI_LINUX

VALIDATION_TEST(SystemFileRoot, write_close_warning) {
  auto root = File::Root::open("/dev"_view);
  ASSERT(root);

  capture_next_file_log();
  Bool written = (*root).write(test_contents, "full"_view);
  EXPECT_NOT(written);
  EXPECT(file_warning_contains(
      "System::File write failed. path=/dev/full "
      "stage=close errno="_view));
}

#endif

VALIDATION_TEST(SystemFileRoot, directory_replacement) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool original_written = temporary.write_member("file", test_contents);
  ASSERT(original_written);

  auto root = File::Root::open(temporary.get_location());
  ASSERT(root);

  Bool moved = temporary.rename_root();
  Bool replacement_created = temporary.create_replacement_root();
  Bool replacement_written =
      temporary.write_member("file", replacement_contents);
  ASSERT(moved);
  ASSERT(replacement_created);
  ASSERT(replacement_written);

  auto source = (*root).read("file"_view);
  ASSERT(source);
  EXPECT_TEXT(*source, replacement_contents);
}

#ifdef PERI_LINUX

VALIDATION_TEST(SystemFileRoot, same_opened_member) {
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
    auto first_source = File::Root::open(temporary.get_location());
    auto second_source = File::Root::open(temporary.get_location());
    ASSERT(first_source);
    ASSERT(second_source);

    File::Root first(static_cast<File::Root&&>(*first_source));
    File::Root moved(static_cast<File::Root&&>(first));
    File::Root assigned(static_cast<File::Root&&>(*second_source));
    assigned = static_cast<File::Root&&>(moved);

    auto source = assigned.read("file"_view);
    ASSERT(source);
    EXPECT_TEXT(*source, test_contents);
  }
}

VALIDATION_TEST(SystemFileRoot, invalid_routes) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Bool written = temporary.write_member("file", test_contents);
  ASSERT(written);

  auto root = File::Root::open(temporary.get_location());
  ASSERT(root);

  U8 embedded_null[] = {'f', 'i', 'l', 'e', '\0', 'x'};
  auto embedded_source =
      (*root).read(View::Bytes(embedded_null, sizeof(embedded_null)));
  EXPECT_NOT(embedded_source);

  Static::Bytes<5> terminated_path = {{'f', 'i', 'l', 'e', '\0'}};
  auto terminated_source = (*root).read(terminated_path);
  ASSERT(terminated_source);
  EXPECT_TEXT(*terminated_source, test_contents);

  Static::Bytes<Path::max_size + 1> overlong;
  for (Count i = 0; i < overlong.get_size(); i++) {
    overlong[i] = 'a';
  }

  auto overlong_source = (*root).read(overlong);
  EXPECT_NOT(overlong_source);
}

// Root is a directory location, so a parent segment can reach a sibling. Keep
// this explicit because lexical normalization used to reject that operation.
VALIDATION_TEST(SystemFileRoot, parent_member) {
  TemporaryRoot temporary;
  ASSERT(temporary);
  ASSERT(temporary.write_outside(test_contents));

  auto root = File::Root::open(temporary.get_location());
  ASSERT(root);
  char relative[temporary_path_capacity];
  const char* name = strrchr(temporary.get_outside_path(), '/');
  ASSERT(name);
  S32 written = snprintf(relative, sizeof(relative), "../%s", name + 1);
  ASSERT(written >= 0);
  Count size = written;
  ASSERT(size < sizeof(relative));

  auto source = root->read(NullTerminated::to_view(relative, size));
  ASSERT(source);
  EXPECT_TEXT(*source, test_contents);
}

// A retained fingerprint describes the same open object as its bytes. Replacing
// the file at that location must produce a different observation.
VALIDATION_TEST(SystemFileRoot, snapshot_identity) {
  TemporaryRoot temporary;
  ASSERT(temporary);
  ASSERT(temporary.write_member("file", test_contents));
  ASSERT(temporary.write_member("replacement", replacement_contents));
  auto root = File::Root::open(temporary.get_location());
  ASSERT(root);
  auto before = root->read_snapshot("file"_view);
  ASSERT(before);
  auto fingerprint = root->fingerprint("file"_view);
  ASSERT(fingerprint);
  EXPECT(before->get_fingerprint() == *fingerprint);

  char source[temporary_path_capacity];
  char destination[temporary_path_capacity];
  ASSERT(temporary.get_member_path("replacement", source, sizeof(source)));
  ASSERT(temporary.get_member_path("file", destination, sizeof(destination)));
  ASSERT(
      File::replace(
          NullTerminated::convert_cstring(source, sizeof(source)),
          NullTerminated::convert_cstring(destination, sizeof(destination))));

  auto after = root->read_snapshot("file"_view);
  ASSERT(after);
  EXPECT_TEXT(before->get_contents(), test_contents);
  EXPECT_TEXT(after->get_contents(), replacement_contents);
  EXPECT(!(before->get_fingerprint() == after->get_fingerprint()));
}
