// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/time.hpp"

#ifdef PERI_WINDOWS
#include <windows.h>
#else
#include <time.h>
#endif

#include "perimortem/core/writer/textual.hpp"

using namespace Perimortem::Core;

constexpr Count nano_to_seconds = Count(1'000'000'000);
const Time boot_timestamp = Time::now();

template <Bool extend_digits = False>
auto write_fixed(Writer::Textual& writer, U64 value) -> void {
  if constexpr (extend_digits) {
    if (value < 100) {
      writer << '0';
    }
  }

  if (value < 10) {
    writer << '0';
  }

  writer << value;
}

auto Time::now() -> Time {

#ifdef PERI_WINDOWS

  LARGE_INTEGER counter, frequency;
  QueryPerformanceCounter(&counter);
  QueryPerformanceFrequency(&frequency);
  const U64 ticks = U64(counter.QuadPart);
  const U64 rate = U64(frequency.QuadPart);
  return Time(
      ticks / rate * nano_to_seconds + ticks % rate * nano_to_seconds / rate);

#else

  timespec time;
  clock_gettime(CLOCK_MONOTONIC, &time);
  return Time(time.tv_sec, time.tv_nsec);

#endif

}

auto Time::clock() -> Time {

#ifdef PERI_WINDOWS

  FILETIME stamp;
  GetSystemTimePreciseAsFileTime(&stamp);
  const U64 ticks = (U64(stamp.dwHighDateTime) << 32) | stamp.dwLowDateTime;
  // FILETIME counts 100 ns intervals since 1601. The public clock uses the
  // Unix epoch, keeping timestamps comparable across native hosts.
  return Time((ticks - 116444736000000000ULL) * 100);

#else

  timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
  return Time(time.tv_sec, time.tv_nsec);

#endif

}

auto Time::boot() -> Time {
  return boot_timestamp;
}

auto Time::calculate_clock() const -> Static::Bytes<12> {
  // Calculate time components.
  U64 seconds_today = (get_stamp() / nano_to_seconds) % 86400;
  auto hours = seconds_today / 3600;
  auto minutes = (seconds_today % 3600) / 60;
  auto seconds = seconds_today % 60;
  auto milliseconds = (get_stamp() / 1'000'000) % 1000;

  Static::Bytes<12> time_string;
  Writer::Textual time_writer(time_string);
  write_fixed(time_writer, hours);
  time_writer << ':';
  write_fixed(time_writer, minutes);
  time_writer << ':';
  write_fixed(time_writer, seconds);
  time_writer << '.';
  write_fixed<True>(time_writer, milliseconds);
  return time_string;
}
