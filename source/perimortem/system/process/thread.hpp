// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/storage/result.hpp"
#include "perimortem/core/view/bytes.hpp"

namespace Perimortem::System::Process {

// Thread runs one callback on a separate native thread through the same API on
// every supported host. It applies the supplied name to Perimortem diagnostics
// and the host debugger, then joins before releasing the native handle.
// Applications can therefore build execution policies independently of the
// platform thread creation and naming APIs.
//
// Startup copies the name into thread local storage before returning. The
// callback context remains caller storage and must stay accessible until the
// callback returns. Moving transfers the handle and its join obligation while
// leaving the source in its default state.
class Thread {
 public:
  using Function = void(void* context);

  enum class StartFailure : U8 {
    HostRejected,
  };

  Thread() = default;
  EXPORTED(PERIMORTEM) ~Thread();
  EXPORTED(PERIMORTEM) Thread(Thread&& source);
  EXPORTED(PERIMORTEM) auto operator=(Thread&& source) -> Thread&;

  Thread(const Thread&) = delete;
  auto operator=(const Thread&) -> Thread& = delete;

  // Starts one native thread and invokes `function` once. Taking a function
  // reference makes a callable callback an input invariant rather than a
  // runtime failure. `StartFailure::HostRejected` reports that the native
  // thread could not be created, including resource exhaustion, without
  // invoking the callback.
  //
  // The complete name is retained for diagnostics while the host receives the
  // platform-supported prefix. An empty name skips the diagnostic name copy and
  // the native naming call. The new thread begins with the spawning thread's
  // diagnostic sink and level.
  static EXPORTED(PERIMORTEM) auto start(
      Core::View::Bytes name,
      Function& function,
      void* context = nullptr) -> Core::Storage::Result<Thread, StartFailure>;

  // Blocks until the callback returns, then releases the native handle. A
  // default Thread and the source of a completed move hold no handle, so
  // repeated calls have no effect.
  EXPORTED(PERIMORTEM) auto join() -> void;

 private:
  U64 handle = 0;
  Bool active = False;
};

}  // namespace Perimortem::System::Process
