// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.hpp"

#include "perimortem/core/scalar.hpp"
#include "perimortem/core/static/bytes.hpp"

namespace Perimortem::Core {

// Time stores a nanosecond stamp from either the monotonic interval clock or
// Unix wall time. A stamp does not record its clock source, so comparison and
// measurement are meaningful only between values obtained from the same source.
//
// `now` supplies the monotonic clock for measuring elapsed time. `clock`
// supplies Unix wall time for timestamps and clock formatting. `boot` preserves
// the monotonic stamp captured during Perimortem initialization.
class Time {
 public:
  // Duration stores the absolute nanosecond distance produced by `measure`.
  // Conversions preserve fractional units rather than truncating subsecond
  // precision.
  class Duration {
   public:
    constexpr Duration(U64 nanoseconds) : nanosecond_delta(nanoseconds) {};

    // Returns the complete duration as nanoseconds.
    constexpr auto convert_to_nanoseconds() const -> U64 {
      return nanosecond_delta;
    }

    // Returns the duration as fractional microseconds.
    constexpr auto convert_to_microseconds() const -> R64 {
      return nanosecond_delta / R64(1'000.0);
    }

    // Returns the duration as fractional milliseconds.
    constexpr auto convert_to_milliseconds() const -> R64 {
      return nanosecond_delta / R64(1'000'000.0);
    }

    // Returns the duration as fractional seconds.
    constexpr auto convert_to_seconds() const -> R64 {
      return (nanosecond_delta / 1'000'000.0) / R64(1'000.0);
    }

   private:
    U64 nanosecond_delta;
  };

  constexpr Time() : timestamp(0) {};
  constexpr Time(U64 stamp) : timestamp(stamp) {};
  constexpr Time(U64 seconds, U64 nanoseconds)
      : timestamp(seconds * 1'000'000'000 + nanoseconds) {};

  // Returns the current platform monotonic stamp. Use it only with other
  // monotonic stamps when comparing or measuring elapsed time.
  static EXPORTED(PERIMORTEM) auto now() -> Time;

  // Returns Unix wall time in nanoseconds. Wall time can change when the system
  // clock is adjusted and must not be used to measure elapsed intervals.
  static EXPORTED(PERIMORTEM) auto clock() -> Time;

  // Returns the monotonic stamp captured during Perimortem initialization.
  static EXPORTED(PERIMORTEM) auto boot() -> Time;

  // Returns the all bits set sentinel, which no supported clock produces.
  static auto never() -> Time { return -1; };

  // Compares the numeric stamps. The caller must supply values obtained from
  // the same clock source.
  constexpr auto operator==(const Time& rhs) const -> Bool {
    return get_stamp() == rhs.get_stamp();
  };

  // Returns the raw nanosecond stamp without identifying its clock source.
  constexpr auto get_stamp() const -> U64 { return timestamp; }

  // Returns the absolute distance between stamps from the same clock source.
  // Requiring both values prevents a wall time stamp from implicitly measuring
  // against the monotonic `now` clock.
  constexpr auto measure(const Time& end_point) const -> Duration {
    auto start_stamp = Core::Scalar::min(get_stamp(), end_point.get_stamp());
    auto end_stamp = Core::Scalar::max(get_stamp(), end_point.get_stamp());
    return Duration(end_stamp - start_stamp);
  }

  // Formats a Unix wall time stamp as `HH:MM:SS.mmm` in UTC. Calling this on a
  // monotonic stamp has no meaningful result.
  EXPORTED(PERIMORTEM) auto calculate_clock() const -> Static::Bytes<12>;

 private:
  U64 timestamp;
};

}  // namespace Perimortem::Core
