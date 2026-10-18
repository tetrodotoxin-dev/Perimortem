// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/storage/result.hpp"
#include "perimortem/core/view/bytes.h"
#include "perimortem/memory/allocator/arena.hpp"

namespace Perimortem::System::Process {

// Library retains one native loader handle in the current process. Each
// successful open returns a distinct handle, and destruction asks the host to
// release it. A rejected release logs an error and leaves final module cleanup
// to the process rather than aborting otherwise valid execution.
//
// A symbol address remains valid while its Library lives. The caller converts
// that address to the function or data type declared by the loaded interface.
// Failure text is retained by the caller's Arena so it remains available after
// the platform loader returns.
class Library {
 public:
  static EXPORTED(PERIMORTEM) auto open(
      Perimortem::Core::View::Bytes path,
      Perimortem::Memory::Allocator::Arena& errors) -> Perimortem::Core::
      Storage::Result<Library, Perimortem::Core::View::Bytes>;

  EXPORTED(PERIMORTEM) Library(Library&& source);
  Library(const Library&) = delete;
  auto operator=(const Library&) -> Library& = delete;
  EXPORTED(PERIMORTEM) ~Library();

  EXPORTED(PERIMORTEM) auto symbol(
      Perimortem::Core::View::Bytes name,
      Perimortem::Memory::Allocator::Arena& errors) const -> Perimortem::Core::
      Storage::Result<void*, Perimortem::Core::View::Bytes>;

 private:
  // Only a successful `open()` can construct Library from a native handle.
  // Explicit private construction prevents conversion from an arbitrary
  // pointer and keeps loader failure in the returned Result.
  explicit Library(void* handle) : handle(handle) {}
  void* handle;
};

}  // namespace Perimortem::System::Process
