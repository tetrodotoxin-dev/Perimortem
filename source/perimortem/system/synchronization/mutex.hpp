// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.hpp"

namespace Perimortem::System::Synchronization {

// Mutex serializes one critical section among threads in the same process.
// The host synchronization object remains opaque so this header and the public
// class layout do not depend on Windows or pthread declarations. Acquisition
// can block until the current Lock ends. Recursive acquisition and fairness are
// not promised. Destruction requires every Lock and concurrent attempt to have
// ended.
class Mutex {
 public:
  // Lock acquires one Mutex for its complete lifetime. Leaving the enclosing
  // scope releases the critical section on every control flow path.
  class Lock {
   public:
    // Implicit conversion would hide lock acquisition at a call site.
    // Construction must visibly mark where the serialized scope begins.
    explicit EXPORTED(PERIMORTEM) Lock(Mutex& mutex);
    EXPORTED(PERIMORTEM) ~Lock();

    Lock(const Lock&) = delete;
    Lock(Lock&&) = delete;
    auto operator=(const Lock&) -> Lock& = delete;
    auto operator=(Lock&&) -> Lock& = delete;

   private:
    Mutex& mutex;
  };

  EXPORTED(PERIMORTEM) Mutex();
  EXPORTED(PERIMORTEM) ~Mutex();

  Mutex(const Mutex&) = delete;
  Mutex(Mutex&&) = delete;
  auto operator=(const Mutex&) -> Mutex& = delete;
  auto operator=(Mutex&&) -> Mutex& = delete;

 private:
  auto lock() -> void;
  auto unlock() -> void;

  void* implementation = nullptr;
};

}  // namespace Perimortem::System::Synchronization
