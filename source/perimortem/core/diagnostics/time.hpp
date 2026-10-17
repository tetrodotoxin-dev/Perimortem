// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.hpp"

namespace Perimortem::Core::Diagnostics {

// Time samples a host monotonic clock for diagnostic timestamps, profiling and
// other instrumentation. Each sample stores a nanosecond position on the
// current host boot timeline. Wall clock corrections cannot reorder those
// positions. They do not identify Unix or calendar time and cannot be compared
// with samples produced during another host boot.
class Time {
 public:
  // Duration stores the absolute nanosecond distance between two monotonic
  // positions. Conversions preserve fractional units instead of truncating
  // subsecond precision.
  class Duration {
    friend Time;

   public:
    constexpr auto convert_to_nanoseconds() const -> U64 {
      return nanosecond_delta;
    }

    constexpr auto convert_to_microseconds() const -> R64 {
      return nanosecond_delta / R64(1'000.0);
    }

    constexpr auto convert_to_milliseconds() const -> R64 {
      return nanosecond_delta / R64(1'000'000.0);
    }

    constexpr auto convert_to_seconds() const -> R64 {
      return nanosecond_delta / R64(1'000'000'000.0);
    }

   private:
    constexpr Duration(U64 nanoseconds) : nanosecond_delta(nanoseconds) {}

    U64 nanosecond_delta;
  };

  constexpr Time() = default;

  // Restores a position from nanoseconds that share the current host monotonic
  // clock's origin. This names the clock domain at the construction boundary
  // instead of accepting an untagged integer conversion.
  static constexpr auto from_nanoseconds(U64 nanoseconds) -> Time {
    return Time(nanoseconds);
  }

  // Samples the current monotonic position normalized to nanoseconds. Samples
  // never decrease while the host is running and wall clock corrections do not
  // affect them. Nanoseconds are the common unit rather than a precision
  // guarantee: Windows derives the value from its performance counter, while
  // Linux and WebAssembly inherit the resolution of their monotonic host
  // clock. Consecutive calls can return the same position, and whether time
  // spent suspended advances the clock remains platform defined.
  static EXPORTED(PERIMORTEM) auto now() -> Time;

  // Provides one shared origin for elapsed diagnostic timestamps. The sample
  // is captured during Perimortem dynamic initialization, so static
  // initialization order and host clock resolution can shift its approximation
  // of module load time by a few milliseconds.
  static EXPORTED(PERIMORTEM) auto boot() -> Time;

  constexpr auto operator==(const Time& rhs) const -> Bool {
    return nanoseconds == rhs.nanoseconds;
  }

  constexpr auto operator<(const Time& rhs) const -> Bool {
    return nanoseconds < rhs.nanoseconds;
  }

  constexpr auto get_nanoseconds() const -> U64 { return nanoseconds; }

  // Returns the absolute distance between two positions from the current host
  // boot. Requiring an endpoint prevents an implicit read from changing the
  // interval while it is being inspected.
  constexpr auto measure(const Time& endpoint) const -> Duration {
    if (nanoseconds < endpoint.nanoseconds) {
      return Duration(endpoint.nanoseconds - nanoseconds);
    }

    return Duration(nanoseconds - endpoint.nanoseconds);
  }

 private:
  constexpr Time(U64 nanoseconds) : nanoseconds(nanoseconds) {}

  U64 nanoseconds = 0;
};

}  // namespace Perimortem::Core::Diagnostics
