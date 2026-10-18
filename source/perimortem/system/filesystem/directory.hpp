// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/data.hpp"
#include "perimortem/core/storage/option.hpp"
#include "perimortem/memory/global/bytes.hpp"
#include "perimortem/system/filesystem/path.hpp"

namespace Perimortem::System::Filesystem {

// Directory is the reusable canonical identity for a Path whose host object
// must be a directory. Only `open()` can establish that state. It emits the
// Path, asks the host to classify the result, then preserves the same Anchor
// and components without retaining a native handle.
//
// Global storage keeps the components available after caller storage expires
// and permits a Directory copy to cross a thread boundary. Each copy receives
// an independent allocation.
//
// Opening observes the host once and does not retain an operating system
// directory handle. Renaming, replacing or removing the location can therefore
// change what the stored Anchor and components reach during a later
// transaction.
// Links are resolved by the host during that observation and can identify a
// directory outside the lexical anchor. Directory provides identity and
// lifetime for the canonical path rather than a filesystem sandbox.
class Directory {
 public:
  Directory(const Directory&) = default;
  auto operator=(const Directory&) -> Directory& = default;
  Directory(Directory&& source) = default;
  auto operator=(Directory&& source) -> Directory& = default;
  ~Directory() = default;

  // Emits and observes `path` through the same native Path policy used by File.
  // Some contains an independent canonical copy when the host currently
  // reports a directory. None reports emission failure, a missing location or
  // any other host object type.
  static EXPORTED(PERIMORTEM) auto open(const Path& path)
      -> Core::Storage::Option<Directory>;

  // Opens the `[work]:/` anchor captured when Perimortem loaded. Later changes
  // to the process working directory do not retarget the returned Directory.
  static EXPORTED(PERIMORTEM) auto current()
      -> Core::Storage::Option<Directory>;

  auto get_anchor() const -> Path::Anchor { return anchor; }

  // Returns the copied canonical components. The View remains valid until the
  // Directory is assigned, moved from or destroyed. Constructing another Path
  // uses these values through the same validated `Path::create()` boundary as
  // every other caller.
  auto get_components() const -> Core::View::Bytes {
    return components.get_view();
  }

 private:
  Directory(Path::Anchor anchor, Memory::Global::Bytes&& components)
      : anchor(anchor), components(Core::Data::take(components)) {}

  Path::Anchor anchor;
  Memory::Global::Bytes components;
};

}  // namespace Perimortem::System::Filesystem
