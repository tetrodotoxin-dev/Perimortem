// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/storage/option.hpp"
#include "perimortem/core/view/bytes.h"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/global/bytes.hpp"
#include "perimortem/system/filesystem/path.hpp"

namespace Perimortem::System::Filesystem {

// File gives synchronous reads, writes and metadata inspection one portable
// transaction boundary. Every operation accepts a validated canonical Path.
// Path emission owns native allocation, anchor resolution, termination and
// platform encoding, allowing foreign APIs to request the same representation
// without reproducing File internals.
//
// A transaction materializes its native path once and keeps it alive through
// the complete host operation. Reads inspect metadata and transfer content from
// one open stream so concurrent path replacement cannot combine observations
// from different files. Links retain their host behavior and may identify
// locations outside the canonical anchor. Path validation is therefore not an
// access boundary.
//
// Reads load regular files up to 64 GiB into one Global Bytes object. Its
// process heap allocation can be released from another thread, but concurrent
// data access still requires synchronization. The complete file must fit in
// process memory because paging and streaming are separate capabilities.
//
// A host without filesystem access rejects every transaction through its
// ordinary failure result. Reads and metadata return None, while boolean
// operations return False. Reads, metadata, writes and replacements log the
// failed transaction stage. Logged transactions establish attribution at the
// public File operation. An existing caller attribution remains authoritative,
// while an unattributed call identifies the File operation instead of a shared
// stage helper.
class File {
 public:
  // Fingerprint uses constant storage to sample host metadata before deciding
  // whether cached file contents should be inspected again. It records the
  // observed device or volume, object index, size, modification time and
  // metadata change time. A mismatch invalidates the cache even when contents
  // happen to remain equal. A match suggests the contents are unchanged but
  // cannot prove it when identifiers are reused or changes preserve the sampled
  // metadata.
  //
  // Host identifiers and timestamp behavior are not portable across operating
  // systems, filesystems or necessarily system boots. Fingerprints are compared
  // only within the host context that produced them.
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

  // Returns a new Global Bytes allocation containing one regular file. Some
  // empty Bytes represents an empty file. None reports a logged emission, open,
  // metadata, content or close failure.
  static EXPORTED(PERIMORTEM) auto read(const Path& path)
      -> Core::Storage::Option<Memory::Global::Bytes>;

  // Reads Fingerprint metadata through one open regular file without reading
  // content. Its storage cost does not depend on file size. None reports a
  // logged emission, open, metadata or close failure.
  static EXPORTED(PERIMORTEM) auto fingerprint(const Path& path)
      -> Core::Storage::Option<Fingerprint>;

  // Reads into the caller Arena and returns a View over that allocation. The
  // View remains accessible until the Arena resets or is destroyed. Some empty
  // View represents an empty file. None reports a logged transaction failure.
  static EXPORTED(PERIMORTEM) auto read(
      Memory::Allocator::Arena& arena,
      const Path& path) -> Core::Storage::Option<Core::View::Bytes>;

  // Creates or truncates the selected file and writes the complete source.
  // Success includes stream closure, including an empty write that truncates
  // the file. False reports a logged emission, open, content or close failure.
  static EXPORTED(PERIMORTEM) auto write(
      Core::View::Bytes data,
      const Path& path) -> Bool;

  // Renames the filesystem object at `source` onto `destination` through one
  // native host operation. On success `destination` resolves to the prior
  // source object and `source` no longer resolves to it. False reports a logged
  // emission or rename failure, including a host refusal to rename across
  // filesystems.
  static EXPORTED(PERIMORTEM) auto replace(
      const Path& source,
      const Path& destination) -> Bool;

  // Delegates removal to the host without preclassifying the selected
  // filesystem object. This preserves native link and directory behavior. The
  // host decides which object types are removable and False reports rejection
  // without logging.
  static EXPORTED(PERIMORTEM) auto remove(const Path& path) -> Bool;

  // Returns True only when the Path currently resolves to a regular file.
  // Directories, other object types, missing paths and host errors return
  // False.
  static EXPORTED(PERIMORTEM) auto exists(const Path& path) -> Bool;
};

}  // namespace Perimortem::System::Filesystem
