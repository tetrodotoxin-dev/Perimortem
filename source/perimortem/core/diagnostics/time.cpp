// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/diagnostics/time.hpp"

#ifdef TOOLCHAIN_WINDOWS
#include <windows.h>
#else
#include <time.h>
#endif

using namespace Perimortem::Core;

constexpr Count nanoseconds_per_second = Count(1'000'000'000);

// One sample shared by the module gives every diagnostic thread the same
// elapsed origin instead of starting a new timeline when each thread first
// logs.
const Diagnostics::Time boot_time = Diagnostics::Time::now();

auto Diagnostics::Time::now() -> Diagnostics::Time {

#ifdef TOOLCHAIN_WINDOWS

  LARGE_INTEGER counter, frequency;
  QueryPerformanceCounter(&counter);
  QueryPerformanceFrequency(&frequency);

  const U64 ticks = U64(counter.QuadPart);
  const U64 rate = U64(frequency.QuadPart);

  // Scale the whole seconds and remaining ticks separately. Multiplying the
  // complete performance counter by one billion would overflow after a long
  // host uptime even when the final nanosecond position remains representable.
  return from_nanoseconds(
      ticks / rate * nanoseconds_per_second +
      ticks % rate * nanoseconds_per_second / rate);

#else

  timespec stamp;
  clock_gettime(CLOCK_MONOTONIC, &stamp);
  return from_nanoseconds(
      U64(stamp.tv_sec) * nanoseconds_per_second + U64(stamp.tv_nsec));

#endif
}

auto Diagnostics::Time::boot() -> Diagnostics::Time {
  return boot_time;
}
