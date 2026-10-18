// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/filesystem/file.hpp"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toolchain/validation/test.hpp"

#ifdef TOOLCHAIN_LINUX
#include <dirent.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>
#elifdef TOOLCHAIN_WINDOWS
#include <windows.h>
#endif

#include "perimortem/core/algorithm/search.hpp"
#include "perimortem/core/data.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/system/filesystem/directory.hpp"
#include "perimortem/system/filesystem/path.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;
using namespace Toolchain::Validation;

using Directory = Filesystem::Directory;
using Path = Filesystem::Path;

static_assert(__is_constructible(Directory, const Directory&));
static_assert(
    __is_same(decltype(Directory::current()), Storage::Option<Directory>));
static_assert(__is_same(
    decltype(Filesystem::File::read("[work]:/file"_path)),
    Storage::Option<Global::Bytes>));
static_assert(__is_same(
    decltype(Filesystem::File::fingerprint("[work]:/file"_path)),
    Storage::Option<Filesystem::File::Fingerprint>));

constexpr auto test_contents = "{\"value\":42}"_view;
constexpr auto replacement_contents = "{\"other\":24}"_view;
static constexpr CppSize temporary_path_capacity = 512;
static constexpr Count captured_file_log_capacity = 1024;
static U64 temporary_path_sequence = 0;

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

static Diagnostics::Log::Level captured_file_log_level;
static Diagnostics::Source captured_file_log_source;
static Storage::Bytes<captured_file_log_capacity> captured_file_log;
static Count captured_file_log_size = 0;
static Count captured_file_log_count = 0;
static Diagnostics::Log::Sink previous_file_log_sink;

static auto capture_file_log(
    Diagnostics::Log::Level level,
    View::Bytes message,
    const Diagnostics::Source& source) -> void {
  constexpr View::Bytes file_diagnostic = "System::Filesystem::File "_view;
  if (Algorithm::Search::match(message, file_diagnostic) != 0) {
    return;
  }

  captured_file_log_level = level;
  captured_file_log_source = source;
  captured_file_log_size = message.get_size();
  captured_file_log = message;
  ++captured_file_log_count;
}

static auto reset_file_logs() -> void {
  captured_file_log_size = 0;
  captured_file_log_count = 0;
  captured_file_log_source = Diagnostics::Source();
}

static auto begin_file_logs() -> void {
  reset_file_logs();
  previous_file_log_sink = Diagnostics::Log::get_sink();
  Diagnostics::Log::set_sink(capture_file_log);
}

static auto end_file_logs() -> void {
  Diagnostics::Log::set_sink(previous_file_log_sink);
}

static auto file_warning_contains(View::Bytes message) -> Bool {
  if (captured_file_log_level != Diagnostics::Log::Level::Warning) {
    return False;
  }

  const View::Bytes captured =
      captured_file_log.slice(0, captured_file_log_size);
  return Algorithm::Search::match(captured, message) != Count(-1);
}

static auto build_member_path(
    char* output,
    CppSize output_size,
    const char* directory,
    const char* member) -> Bool {
  const S32 written = snprintf(output, output_size, "%s/%s", directory, member);
  return written > 0 && CppSize(written) < output_size;
}

#ifdef TOOLCHAIN_WINDOWS

static auto native_create_directory(const char* path) -> Bool {
  return CreateDirectoryA(path, nullptr) != 0;
}

static auto native_change_directory(const char* path) -> Bool {
  return SetCurrentDirectoryA(path) != 0;
}

static auto native_change_directory(const Path::Native& path) -> Bool {
  return SetCurrentDirectoryW(path.get_data()) != 0;
}

static auto native_is_directory(const char* path) -> Bool {
  const U32 attributes = GetFileAttributesA(path);
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY);
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
    const S32 written = snprintf(pattern, sizeof(pattern), "%s/*", path);
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

static auto native_resize_file(const char* path, U64 size) -> Bool {
  HANDLE file = CreateFileA(
      path, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return False;
  }

  LARGE_INTEGER offset;
  offset.QuadPart = size;

  const Bool resized =
      SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) && SetEndOfFile(file);
  CloseHandle(file);
  return resized;
}

#else

static auto native_create_directory(const char* path) -> Bool {
  return mkdir(path, 0700) == 0;
}

static auto native_change_directory(const char* path) -> Bool {
  return chdir(path) == 0;
}

static auto native_change_directory(const Path::Native& path) -> Bool {
  return chdir(path.get_data()) == 0;
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

static auto native_resize_file(const char* path, U64 size) -> Bool {
  return truncate(path, S64(size)) == 0;
}

#endif

static auto native_process_id() -> U64 {

#ifdef TOOLCHAIN_WINDOWS

  return GetCurrentProcessId();

#else

  return U64(getpid());

#endif
}

static auto create_temporary_directory(
    char* native_path,
    CppSize native_capacity,
    char* component,
    CppSize component_capacity) -> Bool {
  const U64 sequence =
      __atomic_fetch_add(&temporary_path_sequence, U64(1), __ATOMIC_RELAXED);
  const S32 component_size = snprintf(
      component, component_capacity, "perimortem_file_%llx_%llx",
      native_process_id(), sequence);
  if (component_size <= 0 || CppSize(component_size) >= component_capacity) {
    return False;
  }

#ifdef TOOLCHAIN_WINDOWS

  char temporary[temporary_path_capacity];
  const U32 size = GetTempPathA(sizeof(temporary), temporary);
  if (!size || size >= sizeof(temporary)) {
    return False;
  }

#else

  const char* temporary = getenv("TMPDIR");
  if (!temporary || temporary[0] != '/') {
    temporary = "/tmp";
  }

#endif

  const char* separator = temporary[strlen(temporary) - 1] == '/' ? "" : "/";
  const S32 written = snprintf(
      native_path, native_capacity, "%s%s%s", temporary, separator, component);
  if (written <= 0 || CppSize(written) >= native_capacity) {
    return False;
  }

  return native_create_directory(native_path);
}

class TemporaryRoot {
 public:
  TemporaryRoot() {
    valid = create_temporary_directory(
        native_root, sizeof(native_root), component, sizeof(component));
  }

  TemporaryRoot(const TemporaryRoot&) = delete;
  auto operator=(const TemporaryRoot&) -> TemporaryRoot& = delete;

  ~TemporaryRoot() {
    if (valid) {
      native_remove_tree(native_root);
    }
  }

  operator bool() const { return bool(valid); }

  auto path(const char* member, char* storage, CppSize capacity) const
      -> Storage::Option<Path> {
    const S32 written =
        member && member[0]
            ? snprintf(storage, capacity, "%s/%s", component, member)
            : snprintf(storage, capacity, "%s", component);
    if (written <= 0 || CppSize(written) >= capacity) {
      return {};
    }

    return Path::create(
        Path::Anchor::Temp,
        View::Bytes(Data::cast<const U8>(storage), Count(written)));
  }

  auto native_member(const char* member, char* storage, CppSize capacity) const
      -> Bool {
    return build_member_path(storage, capacity, native_root, member);
  }

  auto write(const char* member, View::Bytes data) const -> Bool {
    char storage[temporary_path_capacity];
    auto selected = path(member, storage, sizeof(storage));
    return selected.visit(
        [&](const Path& value) { return Filesystem::File::write(data, value); },
        [] { return False; });
  }

  auto read(const char* member) const -> Storage::Option<Global::Bytes> {
    char storage[temporary_path_capacity];
    auto selected = path(member, storage, sizeof(storage));
    return selected.visit(
        [](const Path& value) { return Filesystem::File::read(value); },
        [] { return Storage::Option<Global::Bytes>(); });
  }

  auto create_directory(const char* member) const -> Bool {
    char native[temporary_path_capacity];
    return native_member(member, native, sizeof(native)) &&
           native_create_directory(native);
  }

  auto get_native_root() const -> const char* { return native_root; }

 private:
  char native_root[temporary_path_capacity]{};
  char component[128]{};
  Bool valid = False;
};

static Harness SystemFile = {
  .name = "System::Filesystem::File",
  .setup = begin_file_logs,
  .teardown = end_file_logs,
};

VALIDATION_TEST(SystemFile, read_and_write) {
  TemporaryRoot temporary;
  ASSERT(temporary);
  ASSERT(temporary.write("file", test_contents));

  auto source = temporary.read("file");
  ASSERT(source);
  expect_contents(source, test_contents, result);

  char storage[temporary_path_capacity];
  auto path = temporary.path("file", storage, sizeof(storage));
  ASSERT(path);
  path.visit(
      [&](const Path& selected) {
        Allocator::Arena arena;
        auto arena_source = Filesystem::File::read(arena, selected);
        ASSERT(arena_source);
        expect_selected(
            arena_source,
            [&](View::Bytes contents) { EXPECT_EQ(contents, test_contents); },
            result);

        ASSERT(Filesystem::File::write(replacement_contents, selected));

        auto replacement = Filesystem::File::read(selected);
        ASSERT(replacement);
        expect_contents(replacement, replacement_contents, result);

        ASSERT(Filesystem::File::write({}, selected));

        auto empty = Filesystem::File::read(selected);
        ASSERT(empty);
        expect_selected(
            empty,
            [&](const Global::Bytes& contents) { EXPECT(contents.is_empty()); },
            result);
      },

      [&] { EXPECT(false); });

  EXPECT_EQ(captured_file_log_count, Count(0));
}

VALIDATION_TEST(SystemFile, missing_and_directory) {
  TemporaryRoot temporary;
  ASSERT(temporary);
  ASSERT(temporary.create_directory("directory"));

  char missing_storage[temporary_path_capacity];
  char directory_storage[temporary_path_capacity];
  auto missing =
      temporary.path("missing", missing_storage, sizeof(missing_storage));
  auto directory =
      temporary.path("directory", directory_storage, sizeof(directory_storage));
  ASSERT(missing);
  ASSERT(directory);

  missing.visit(
      [&](const Path& selected) {
        EXPECT_NOT(Filesystem::File::read(selected));
        EXPECT_NOT(Filesystem::File::exists(selected));
      },

      [&] { EXPECT(false); });
  directory.visit(
      [&](const Path& selected) {
        EXPECT_NOT(Filesystem::File::read(selected));
        EXPECT_NOT(Filesystem::File::exists(selected));
      },

      [&] { EXPECT(false); });

  EXPECT_EQ(captured_file_log_count, Count(2));
  EXPECT(file_warning_contains("stage=metadata errno="_view));
}

VALIDATION_TEST(SystemFile, replace_remove_and_exists) {
  TemporaryRoot temporary;
  ASSERT(temporary);
  ASSERT(temporary.write("source", test_contents));
  ASSERT(temporary.write("destination", replacement_contents));

  char source_storage[temporary_path_capacity];
  char destination_storage[temporary_path_capacity];
  auto source =
      temporary.path("source", source_storage, sizeof(source_storage));
  auto destination = temporary.path(
      "destination", destination_storage, sizeof(destination_storage));
  ASSERT(source);
  ASSERT(destination);

  source.visit(
      [&](const Path& selected_source) {
        destination.visit(
            [&](const Path& selected_destination) {
              ASSERT(Filesystem::File::exists(selected_source));
              ASSERT(
                  Filesystem::File::replace(
                      selected_source, selected_destination));
              EXPECT_NOT(Filesystem::File::exists(selected_source));

              auto contents = Filesystem::File::read(selected_destination);
              ASSERT(contents);
              expect_contents(contents, test_contents, result);

              ASSERT(Filesystem::File::remove(selected_destination));
              EXPECT_NOT(Filesystem::File::exists(selected_destination));
            },

            [&] { EXPECT(false); });
      },

      [&] { EXPECT(false); });

  EXPECT_EQ(captured_file_log_count, Count(0));
}

VALIDATION_TEST(SystemFile, fingerprint_cache_validation) {
  TemporaryRoot temporary;
  ASSERT(temporary);
  ASSERT(temporary.write("file", test_contents));
  ASSERT(temporary.write("replacement", replacement_contents));

  char file_storage[temporary_path_capacity];
  char replacement_storage[temporary_path_capacity];
  auto file = temporary.path("file", file_storage, sizeof(file_storage));
  auto replacement = temporary.path(
      "replacement", replacement_storage, sizeof(replacement_storage));
  ASSERT(file);
  ASSERT(replacement);

  file.visit(
      [&](const Path& selected_file) {
        auto before = Filesystem::File::fingerprint(selected_file);
        ASSERT(before);
        replacement.visit(
            [&](const Path& selected_replacement) {
              ASSERT(
                  Filesystem::File::replace(
                      selected_replacement, selected_file));
            },

            [&] { EXPECT(false); });

        auto after = Filesystem::File::fingerprint(selected_file);
        ASSERT(after);
        before.visit(
            [&](const Filesystem::File::Fingerprint& old_fingerprint) {
              after.visit(
                  [&](const Filesystem::File::Fingerprint& new_fingerprint) {
                    EXPECT_NOT(old_fingerprint == new_fingerprint);
                  },

                  [&] { EXPECT(false); });
            },

            [&] { EXPECT(false); });
      },

      [&] { EXPECT(false); });

  EXPECT_EQ(captured_file_log_count, Count(0));
}

VALIDATION_TEST(SystemFile, fingerprint_exceeds_read_limit) {
  TemporaryRoot temporary;
  ASSERT(temporary);
  ASSERT(temporary.write("large", {}));

  char native[temporary_path_capacity];
  ASSERT(temporary.native_member("large", native, sizeof(native)));

  constexpr U64 oversized = (U64(1) << 36) + 1;
  if (!native_resize_file(native, oversized)) {
    SKIP("The host filesystem cannot create the sparse test file."_view);
  }

  char storage[temporary_path_capacity];
  auto path = temporary.path("large", storage, sizeof(storage));
  ASSERT(path);
  path.visit(
      [&](const Path& selected) {
        auto fingerprint = Filesystem::File::fingerprint(selected);
        ASSERT(fingerprint);
        fingerprint.visit(
            [&](const Filesystem::File::Fingerprint& value) {
              EXPECT_EQ(value.get_size(), Count(oversized));
            },

            [&] { EXPECT(false); });
        EXPECT_NOT(Filesystem::File::read(selected));
      },

      [&] { EXPECT(false); });

  EXPECT_EQ(captured_file_log_count, Count(1));
  EXPECT(file_warning_contains("stage=size value="_view));
}

VALIDATION_TEST(SystemFile, canonical_diagnostic_path) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  char storage[temporary_path_capacity];
  auto missing = temporary.path("missing", storage, sizeof(storage));
  ASSERT(missing);
  reset_file_logs();
  missing.visit(
      [&](const Path& selected) {
        EXPECT_NOT(Filesystem::File::read(selected));
      },

      [&] { EXPECT(false); });

  EXPECT(file_warning_contains("path=[temp]:/"_view));
  EXPECT(file_warning_contains("stage=open errno="_view));
  EXPECT_EQ(captured_file_log_count, Count(1));
  EXPECT(captured_file_log_source.is_set());
  EXPECT(captured_file_log_source.get_line() > 0);
  EXPECT(
      Algorithm::Search::match(
          captured_file_log_source.get_file(),
          "source/perimortem/system/filesystem/file.cpp"_view) != Count(-1));

  constexpr Diagnostics::Source caller_source(
      "caller.cpp"_view, Count(42), Count(7));
  reset_file_logs();
  {
    auto caller_attribution = Diagnostics::Log::set_attribution(caller_source);
    missing.visit(
        [&](const Path& selected) {
          EXPECT_NOT(Filesystem::File::read(selected));
        },

        [&] { EXPECT(false); });
  }

  EXPECT_EQ(captured_file_log_source.get_file(), "caller.cpp"_view);
  EXPECT_EQ(captured_file_log_source.get_line(), Count(42));
  EXPECT_EQ(captured_file_log_source.get_column(), Count(7));
  EXPECT_EQ(captured_file_log_count, Count(1));
}

#ifdef TOOLCHAIN_LINUX

VALIDATION_TEST(SystemFile, close_failure) {
  constexpr Path full = "[root]:/dev/full"_path;
  reset_file_logs();
  EXPECT_NOT(Filesystem::File::write(test_contents, full));
  EXPECT(file_warning_contains(
      "System::Filesystem::File write failed. path=[root]:/dev/full "
      "stage=close errno="_view));
  EXPECT_EQ(captured_file_log_count, Count(1));
}

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
    const char* components,
    View::Bytes expected,
    Bool expected_present) -> void {
  auto path = Path::create(
      Path::Anchor::Temp,
      View::NullTerminated(components, temporary_path_capacity).get_view());
  if (!path) {
    _exit(21);
  }

  if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) != 0) {
    _exit(20);
  }

  raise(SIGSTOP);

  auto source = path.visit(
      [](const Path& selected) { return Filesystem::File::read(selected); },
      [] { return Storage::Option<Global::Bytes>(); });
  if (!expected_present) {
    _exit(source ? 1 : 0);
  }

  const Bool matches = source.visit(
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
    return rename(replacement, path) == 0;
  }

  return truncate(path, 0) == 0;
}

static auto trace_read(
    const char* path,
    const char* components,
    View::Bytes expected,
    Bool expected_present,
    TraceMutation mutation,
    const char* replacement = nullptr) -> Bool {
  const pid_t child = fork();
  if (child == 0) {
    traced_child(components, expected, expected_present);
  }

  if (child < 0) {
    return False;
  }

  S32 status = 0;
  pid_t waited = waitpid(child, &status, 0);
  if (waited != child || !WIFSTOPPED(status)) {
    return False;
  }

  const S64 option_result =
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
    if (ptrace(PTRACE_SYSCALL, child, nullptr, nullptr) != 0) {
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
    if (ptrace(PTRACE_GETREGS, child, nullptr, &registers) != 0) {
      kill(child, SIGKILL);
      waitpid(child, &status, 0);
      return False;
    }

    const S64 call = S64(registers.orig_rax);
    if (entering && mutation == TraceMutation::TruncateAfterMetadata &&
        opened && call == SYS_read && !mutated) {
      mutated = mutate_path(mutation, path, replacement);
    }

    entering = !entering;
    if (entering) {
      const S64 call_result = S64(registers.rax);
      if (is_open_call(call) && call_result >= 0) {
        opened = True;
        if (mutation == TraceMutation::ReplacePath && !mutated) {
          mutated = mutate_path(mutation, path, replacement);
        }
      } else if (
          is_metadata_call(call) && call_result == 0 &&
          mutation == TraceMutation::TruncateAfterMetadata && opened &&
          !mutated) {
        mutated = mutate_path(mutation, path, replacement);
      }
    }
  }
}

VALIDATION_TEST(SystemFile, opened_stream_identity) {
  TemporaryRoot temporary;
  ASSERT(temporary);
  ASSERT(temporary.write("file", test_contents));
  ASSERT(temporary.write("replacement", replacement_contents));

  char path[temporary_path_capacity];
  char replacement[temporary_path_capacity];
  char components[temporary_path_capacity];
  ASSERT(temporary.native_member("file", path, sizeof(path)));
  ASSERT(
      temporary.native_member("replacement", replacement, sizeof(replacement)));
  ASSERT(temporary.path("file", components, sizeof(components)));

  EXPECT(trace_read(
      path, components, test_contents, True, TraceMutation::ReplacePath,
      replacement));
  EXPECT_EQ(captured_file_log_count, Count(0));
}

VALIDATION_TEST(SystemFile, changed_open_stream_size) {
  TemporaryRoot temporary;
  ASSERT(temporary);
  ASSERT(temporary.write("file", test_contents));

  char path[temporary_path_capacity];
  char components[temporary_path_capacity];
  ASSERT(temporary.native_member("file", path, sizeof(path)));
  ASSERT(temporary.path("file", components, sizeof(components)));

  EXPECT(trace_read(
      path, components, {}, False, TraceMutation::TruncateAfterMetadata));
  EXPECT_EQ(captured_file_log_count, Count(0));
}

#endif

static Harness SystemDirectory = {
  .name = "System::Filesystem::Directory",
};

VALIDATION_TEST(SystemDirectory, current) {
  auto directory = Directory::current();
  ASSERT(directory);
  directory.visit(
      [&](const Directory& selected) {
        EXPECT(selected.get_anchor() == Path::Anchor::Work);
        EXPECT(selected.get_components().is_empty());
      },

      [&] { EXPECT(false); });
}

VALIDATION_TEST(SystemDirectory, captured_work_anchor) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  constexpr Path work = "[work]:/"_path;
  auto original = work.emit();
  Bool entered = False;
  Bool stable = False;
  Bool restored = False;
  original.visit(
      [&](const Path::Native& original_path) {
        entered = native_change_directory(temporary.get_native_root());
        if (entered) {
          auto repeated = work.emit();
          stable = repeated.visit(
              [&](const Path::Native& repeated_path) {
                return Bool(
                    repeated_path.get_view() == original_path.get_view());
              },

              [](Path::EmissionFailure) { return False; });
        }

        restored = native_change_directory(original_path);
      },

      [&](Path::EmissionFailure) {});

  ASSERT(entered);
  ASSERT(restored);
  EXPECT(stable);
}

VALIDATION_TEST(SystemDirectory, open_and_copy) {
  TemporaryRoot temporary;
  ASSERT(temporary);

  Storage::Option<Directory> directory;
  Global::Bytes expected_components;
  {
    char storage[temporary_path_capacity];
    auto path = temporary.path(nullptr, storage, sizeof(storage));
    ASSERT(path);
    path.visit(
        [&](const Path& selected_path) {
          expected_components = selected_path.get_components();
          directory = Directory::open(selected_path);
        },

        [&] { EXPECT(false); });
  }

  ASSERT(directory);
  directory.visit(
      [&](Directory& selected) {
        EXPECT_EQ(selected.get_components(), expected_components.get_view());

        Directory copied(selected);
        EXPECT(copied.get_anchor() == selected.get_anchor());
        EXPECT_EQ(copied.get_components(), expected_components.get_view());

        Directory moved(Data::take(copied));
        EXPECT(moved.get_anchor() == selected.get_anchor());
        EXPECT_EQ(moved.get_components(), expected_components.get_view());
      },

      [&] { EXPECT(false); });
}

VALIDATION_TEST(SystemDirectory, rejects_file_and_missing_path) {
  TemporaryRoot temporary;
  ASSERT(temporary);
  ASSERT(temporary.write("file", test_contents));

  char file_storage[temporary_path_capacity];
  char missing_storage[temporary_path_capacity];
  auto file = temporary.path("file", file_storage, sizeof(file_storage));
  auto missing =
      temporary.path("missing", missing_storage, sizeof(missing_storage));
  ASSERT(file);
  ASSERT(missing);

  file.visit(
      [&](const Path& selected) { EXPECT_NOT(Directory::open(selected)); },

      [&] { EXPECT(false); });
  missing.visit(
      [&](const Path& selected) { EXPECT_NOT(Directory::open(selected)); },

      [&] { EXPECT(false); });
}
