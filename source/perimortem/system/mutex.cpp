// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/mutex.hpp"

#ifdef TOOLCHAIN_WINDOWS
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "toolchain/memory/lifetime.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/memory/allocator/global.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;
using namespace Toolchain::Memory;

#ifdef TOOLCHAIN_WINDOWS

using NativeMutex = SRWLOCK;

#else

using NativeMutex = pthread_mutex_t;

#endif

static_assert(
    alignof(NativeMutex) <= Allocator::Global::allocation_alignment,
    "native mutex exceeds the process heap alignment");

static auto get_native(void* implementation) -> NativeMutex& {
  return *Data::cast<NativeMutex>(implementation);
}

Mutex::Lock::Lock(Mutex& mutex) : mutex(mutex) {
  mutex.lock();
}

Mutex::Lock::~Lock() {
  mutex.unlock();
}

Mutex::Mutex() {
  auto allocation = Allocator::Global::allocate(sizeof(NativeMutex));
  implementation = allocation.get_data();

  auto& native = Lifetime::construct<NativeMutex>(implementation);

#ifdef TOOLCHAIN_WINDOWS

  InitializeSRWLock(&native);

#else

  if (pthread_mutex_init(&native, nullptr) != 0) {
    native.~NativeMutex();
    Allocator::Global::release(Data::cast<U8>(implementation));
    Diagnostics::Log::fatal("Unable to initialize a native mutex."_view);
  }

#endif
}

Mutex::~Mutex() {
  auto& native = get_native(implementation);

#ifndef TOOLCHAIN_WINDOWS

  if (pthread_mutex_destroy(&native) != 0) {
    Diagnostics::Log::fatal("Unable to destroy a native mutex."_view);
  }

#endif

  native.~NativeMutex();
  Allocator::Global::release(Data::cast<U8>(implementation));
}

auto Mutex::lock() -> void {
  auto& native = get_native(implementation);

#ifdef TOOLCHAIN_WINDOWS

  AcquireSRWLockExclusive(&native);

#else

  if (pthread_mutex_lock(&native) != 0) {
    Diagnostics::Log::fatal("Unable to lock a native mutex."_view);
  }

#endif
}

auto Mutex::unlock() -> void {
  auto& native = get_native(implementation);

#ifdef TOOLCHAIN_WINDOWS

  ReleaseSRWLockExclusive(&native);

#else

  if (pthread_mutex_unlock(&native) != 0) {
    Diagnostics::Log::fatal("Unable to unlock a native mutex."_view);
  }

#endif
}
