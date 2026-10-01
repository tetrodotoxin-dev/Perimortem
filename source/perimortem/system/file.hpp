// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/view/bytes.hpp"
#include "perimortem/core/option.hpp"

#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/dynamic/bytes.hpp"

namespace Perimortem::System {

// File owns native filesystem transactions. Root supplies a directory location
// for relative operations. A host without filesystem access rejects these
// operations through their ordinary failure results. Reads and Root
// acquisition return None, while boolean operations return False. This lets
// callers select another source without a platform dependent interface.
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

  // Snapshot couples owned bytes with metadata taken from the same opened
  // object. Callers may probe the Fingerprint before reusing those bytes.
  class Snapshot {
   public:
    Snapshot(Memory::Dynamic::Bytes&& contents, Fingerprint fingerprint)
        : contents(static_cast<Memory::Dynamic::Bytes&&>(contents)),
          fingerprint(fingerprint) {}

    constexpr auto get_contents() const -> Core::View::Bytes {
      return contents.get_view();
    }

    auto take_contents() -> Memory::Dynamic::Bytes {
      return static_cast<Memory::Dynamic::Bytes&&>(contents);
    }

    constexpr auto get_fingerprint() const -> Fingerprint {
      return fingerprint;
    }

   private:
    Memory::Dynamic::Bytes contents;
    Fingerprint fingerprint;
  };

  // Uses a logical directory location as the working root for repeated relative
  // operations. Paths use the host filesystem's ordinary parent and link
  // resolution, and absolute paths select their own origin. Root does not
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

    static auto open(Core::View::Bytes location) -> Core::Option<Root>;
    // Member reads and fingerprints are probes. Absence and I/O failure use
    // the optional result without publishing an ambient diagnostic. The owner
    // that requested the path supplies its authored context.
    auto read(Core::View::Bytes relative_path) const
        -> Core::Option<Memory::Dynamic::Bytes>;
    auto read_snapshot(Core::View::Bytes relative_path) const
        -> Core::Option<Snapshot>;
    auto fingerprint(Core::View::Bytes relative_path) const
        -> Core::Option<Fingerprint>;
    // Retains successful bytes in the caller Arena.
    auto read(Memory::Allocator::Arena& arena, Core::View::Bytes relative_path)
        const -> Core::Option<Core::View::Bytes>;
    auto write(Core::View::Bytes data, Core::View::Bytes relative_path) const
        -> Bool;
    auto remove(Core::View::Bytes relative_path) const -> Bool;
    auto exists(Core::View::Bytes relative_path) const -> Bool;

   private:
    Root(Core::View::Bytes location) : location(location) {}

    Memory::Dynamic::Bytes location;
  };

  static auto read(Core::View::Bytes location)
      -> Core::Option<Memory::Dynamic::Bytes>;
  // Retains successful bytes in the caller Arena.
  static auto read(Memory::Allocator::Arena& arena, Core::View::Bytes location)
      -> Core::Option<Core::View::Bytes>;
  static auto write(Core::View::Bytes data, Core::View::Bytes location) -> Bool;
  // Atomically replaces the destination name when the host filesystem permits
  // one native rename transaction.
  static auto replace(Core::View::Bytes source, Core::View::Bytes destination)
      -> Bool;
  static auto remove(Core::View::Bytes location) -> Bool;
  static auto exists(Core::View::Bytes location) -> Bool;
};

}  // namespace Perimortem::System
