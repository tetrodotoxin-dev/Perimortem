// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/synchronization/mutex.hpp"

#include <stdio.h>
#include <stdlib.h>

#ifdef TOOLCHAIN_WINDOWS
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "toolchain/memory/lifetime.hpp"

#include "perimortem/core/data.hpp"

using namespace Perimortem::Core;
using namespace Toolchain::Memory;

#ifdef TOOLCHAIN_WINDOWS

using NativeMutex = SRWLOCK;

#else

using NativeMutex = pthread_mutex_t;

#endif

static_assert(sizeof(NativeMutex) <= 64, "native mutex exceeds opaque storage");
static_assert(
    alignof(NativeMutex) <= 16,
    "native mutex exceeds opaque alignment");

static auto get_native(U8* implementation) -> NativeMutex& {
  return *Data::cast<NativeMutex>(implementation);
}

// Diagnostics::Log uses Mutex to serialize shared sinks. A native mutex failure
// cannot reenter Log without depending on the failed synchronization path, so
// the primitive reports directly to stderr before terminating the process.
[[noreturn]] static auto native_failure(const char* message) -> void {
  fputs(message, stderr);
  fputc('\n', stderr);
  fflush(stderr);
  abort();
}

Synchronization::Mutex::Lock::Lock(Mutex& mutex) : mutex(mutex) {
  mutex.lock();
}

Synchronization::Mutex::Lock::~Lock() {
  mutex.unlock();
}

Synchronization::Mutex::Mutex() {
  auto& native = Lifetime::construct<NativeMutex>(implementation);

#ifdef TOOLCHAIN_WINDOWS

  InitializeSRWLock(&native);

#else

  if (pthread_mutex_init(&native, nullptr) != 0) {
    native.~NativeMutex();
    native_failure(
        "Core::Synchronization::Mutex could not initialize its native mutex.");
  }

#endif
}

Synchronization::Mutex::~Mutex() {
  auto& native = get_native(implementation);

#ifndef TOOLCHAIN_WINDOWS

  if (pthread_mutex_destroy(&native) != 0) {
    native_failure(
        "Core::Synchronization::Mutex was destroyed while still in use.");
  }

#endif

  native.~NativeMutex();
}

auto Synchronization::Mutex::lock() -> void {
  auto& native = get_native(implementation);

#ifdef TOOLCHAIN_WINDOWS

  AcquireSRWLockExclusive(&native);

#else

  if (pthread_mutex_lock(&native) != 0) {
    native_failure(
        "Core::Synchronization::Mutex could not acquire its native mutex.");
  }

#endif
}

auto Synchronization::Mutex::unlock() -> void {
  auto& native = get_native(implementation);

#ifdef TOOLCHAIN_WINDOWS

  ReleaseSRWLockExclusive(&native);

#else

  if (pthread_mutex_unlock(&native) != 0) {
    native_failure(
        "Core::Synchronization::Mutex could not release its native mutex.");
  }

#endif
}
