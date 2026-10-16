// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/data.hpp"
#include "perimortem/core/storage/option.hpp"
#include "perimortem/core/view/bytes.h"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/global/bytes.hpp"

namespace Perimortem::System::Filesystem {

// File performs synchronous transactions through bounded host paths. One
// trailing null is accepted but embedded nulls are rejected. Separators are
// normalized before the host resolves parent segments and links.
//
// Reads load regular files up to 64 GiB into one Global Bytes object. Its
// process heap allocation can be released from another thread, but concurrent
// data access still requires synchronization. The complete file must fit in
// process memory because paging and streaming aren't supported. Metadata and
// content come from one open stream so concurrent path replacement cannot
// combine two files into one result.
//
// A host without filesystem access rejects every transaction through its
// ordinary failure result with reads and root acquisition returning `None`.
// Reads, writes and replacements log additional diagnostics for the failed
// transaction stage.
class File {
 public:
  // Fingerprint identifies the exact regular filesystem object observed by a
  // File transaction. Size and modification time detect in place changes,
  // while device and inode detect replacement at the same path.
  class Fingerprint {
   public:
    constexpr Fingerprint(
        U64 device = 0,
        U64 node = 0,
        U64 size = 0,
        S64 modified_seconds = 0,
        S64 modified_nanoseconds = 0,
        S64 changed_seconds = 0,
        S64 changed_nanoseconds = 0)
        : device(device),
          node(node),
          size(size),
          modified_seconds(modified_seconds),
          modified_nanoseconds(modified_nanoseconds),
          changed_seconds(changed_seconds),
          changed_nanoseconds(changed_nanoseconds) {}

    constexpr auto operator==(const Fingerprint& rhs) const -> Bool {
      return device == rhs.device && node == rhs.node && size == rhs.size &&
             modified_seconds == rhs.modified_seconds &&
             modified_nanoseconds == rhs.modified_nanoseconds &&
             changed_seconds == rhs.changed_seconds &&
             changed_nanoseconds == rhs.changed_nanoseconds;
    }

    constexpr auto get_size() const -> Count { return Count(size); }

   private:
    U64 device;
    U64 node;
    U64 size;
    S64 modified_seconds;
    S64 modified_nanoseconds;
    S64 changed_seconds;
    S64 changed_nanoseconds;
  };

  // Snapshot stores Global Bytes with metadata taken from the same opened
  // object. Callers may probe the Fingerprint before reusing those values or
  // transferring them to another thread.
  class Snapshot {
   public:
    Snapshot(Memory::Global::Bytes&& contents, Fingerprint fingerprint)
        : contents(Core::Data::take(contents)), fingerprint(fingerprint) {}

    constexpr auto get_contents() const -> Core::View::Bytes {
      return contents.get_view();
    }

    auto take_contents() -> Memory::Global::Bytes {
      return Core::Data::take(contents);
    }

    constexpr auto get_fingerprint() const -> Fingerprint {
      return fingerprint;
    }

   private:
    Memory::Global::Bytes contents;
    Fingerprint fingerprint;
  };

  // Uses a logical directory location as the working root for repeated relative
  // operations. Paths use the host filesystem's ordinary parent and link
  // resolution, while absolute paths ignore the configured root. Root does not
  // restrict access outside of its subfolders.
  //
  // The location is looked up for each transaction. Renaming or replacing that
  // directory changes what later operations observe. A relative root location
  // is interpreted against the working directory at the time of each call.
  class Root {
   public:
    Root(const Root&) = delete;
    auto operator=(const Root&) -> Root& = delete;
    Root(Root&& source) = default;
    auto operator=(Root&& source) -> Root& = default;
    ~Root() = default;

    // Copies a directory path into Global storage. None reports an invalid
    // path, an absent location or a location that is not a directory. Root
    // retains no directory handle and resolves the copied path for every later
    // transaction.
    static EXPORTED(PERIMORTEM) auto open(Core::View::Bytes location)
        -> Core::Storage::Option<Root>;

    // Returns a new Global Bytes allocation containing one regular file. Some
    // empty Bytes represents an empty file. None silently reports path, open,
    // metadata, content or close failure.
    EXPORTED(PERIMORTEM) auto read(Core::View::Bytes relative_path) const
        -> Core::Storage::Option<Memory::Global::Bytes>;

    // Couples Global values with the Fingerprint from the same open stream so a
    // path replacement cannot mix metadata from another file. None silently
    // reports any transaction failure.
    EXPORTED(PERIMORTEM) auto read_snapshot(Core::View::Bytes relative_path)
        const -> Core::Storage::Option<Snapshot>;

    // Observes metadata through an open regular file without reading content.
    // None silently reports path, open, metadata or close failure.
    EXPORTED(PERIMORTEM) auto fingerprint(Core::View::Bytes relative_path) const
        -> Core::Storage::Option<Fingerprint>;

    // Reads into the caller Arena and returns a View over that allocation. The
    // View remains accessible until the Arena resets or is destroyed. Some
    // empty View represents an empty file. None silently reports failure.
    EXPORTED(PERIMORTEM) auto read(
        Memory::Allocator::Arena& arena,
        Core::View::Bytes relative_path) const
        -> Core::Storage::Option<Core::View::Bytes>;

    // Creates or truncates the selected file and writes the complete source.
    // Success includes stream closure, including an empty write that truncates
    // the file. False reports any path, open, content or close failure.
    EXPORTED(PERIMORTEM) auto write(
        Core::View::Bytes data,
        Core::View::Bytes relative_path) const -> Bool;

    // Delegates removal to the host without preclassifying the selected
    // filesystem entry. This preserves native link and directory behavior. The
    // host decides which entry types are removable and False reports rejection.
    EXPORTED(PERIMORTEM) auto remove(Core::View::Bytes relative_path) const
        -> Bool;

    // Returns True only when the selected location resolves to a regular file.
    // Directories, other entry types, missing paths and host errors return
    // False.
    EXPORTED(PERIMORTEM) auto exists(Core::View::Bytes relative_path) const
        -> Bool;

   private:
    Root(Core::View::Bytes location) : location(location) {}

    Memory::Global::Bytes location;
  };

  // Returns a new Global Bytes allocation containing one regular file. Some
  // empty Bytes represents an empty file. None reports a logged path, open,
  // metadata, content or close failure.
  static EXPORTED(PERIMORTEM) auto read(Core::View::Bytes location)
      -> Core::Storage::Option<Memory::Global::Bytes>;

  // Reads into the caller Arena and returns a View over that allocation. The
  // View remains accessible until the Arena resets or is destroyed. Some empty
  // View represents an empty file. None reports a logged transaction failure.
  static EXPORTED(PERIMORTEM) auto read(
      Memory::Allocator::Arena& arena,
      Core::View::Bytes location) -> Core::Storage::Option<Core::View::Bytes>;

  // Creates or truncates the selected file and writes the complete source.
  // Success includes stream closure, including an empty write that truncates
  // the file. False reports a logged path, open, content or close failure.
  static EXPORTED(PERIMORTEM) auto write(
      Core::View::Bytes data,
      Core::View::Bytes location) -> Bool;

  // Renames the filesystem entry at `source` onto `destination` through one
  // native host operation. On success `destination` resolves to the prior
  // source entry. Distinct source and destination directory entries leave no
  // source entry behind. False reports a logged path conversion or rename
  // failure, including a host refusal to rename across filesystems.
  static EXPORTED(PERIMORTEM) auto replace(
      Core::View::Bytes source,
      Core::View::Bytes destination) -> Bool;

  // Delegates removal to the host without preclassifying the selected
  // filesystem entry. This preserves native link and directory behavior. The
  // host decides which entry types are removable and False reports rejection
  // without logging.
  static EXPORTED(PERIMORTEM) auto remove(Core::View::Bytes location) -> Bool;

  // Returns True only when the location resolves to a regular file.
  // Directories, other entry types, missing paths and host errors return False.
  static EXPORTED(PERIMORTEM) auto exists(Core::View::Bytes location) -> Bool;
};

}  // namespace Perimortem::System::Filesystem
