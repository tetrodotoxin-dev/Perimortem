// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/algorithm/hash.hpp"

namespace Perimortem::Validation {

// MoveOnlyValue gives capability tests one scalar whose source changes during
// move construction and which cannot satisfy a copy requirement. Equality and
// hashing let the same fixture test stored values and associative keys without
// defining a new program identity in each implementation file.
template <typename value_type>
class MoveOnlyValue {
  static_assert(__is_integral(value_type));

 public:
  constexpr MoveOnlyValue() = default;

  // Explicit construction prevents a scalar argument from silently becoming a
  // fixture that cannot be copied when the operation also accepts that scalar.
  constexpr explicit MoveOnlyValue(value_type value) : value(value) {}

  MoveOnlyValue(const MoveOnlyValue&) = delete;
  constexpr MoveOnlyValue(MoveOnlyValue&& source) : value(source.value) {
    source.value = value_type();
  }

  constexpr auto get_value() const -> value_type { return value; }
  constexpr auto hash() const -> U64 {
    return Core::Algorithm::Hash(value).get_value();
  }

  constexpr auto operator==(const MoveOnlyValue& rhs) const -> Bool {
    return value == rhs.value;
  }

 private:
  value_type value = 0;
};

// TrackedValue counts each current object from construction through
// destruction. Map and Set tests use the same fixture so their lifetime checks
// measure policy behavior rather than differences between handwritten probes.
class TrackedValue {
 public:
  struct Counts {
    Count constructions = 0;
    Count destructions = 0;
  };

  TrackedValue(Count value, Counts& counts) : value(value), counts(&counts) {
    counts.constructions++;
  }

  TrackedValue(const TrackedValue& source)
      : value(source.value), counts(source.counts) {
    counts->constructions++;
  }

  TrackedValue(TrackedValue&& source)
      : value(source.value), counts(source.counts) {
    counts->constructions++;
  }

  ~TrackedValue() { counts->destructions++; }

  constexpr auto hash() const -> U64 {
    return Core::Algorithm::Hash(value).get_value();
  }

  constexpr auto operator==(const TrackedValue& rhs) const -> Bool {
    return value == rhs.value;
  }

 private:
  Count value = 0;
  Counts* counts;
};

}  // namespace Perimortem::Validation
