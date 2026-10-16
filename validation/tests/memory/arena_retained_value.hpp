// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/algorithm/hash.hpp"

namespace Perimortem::Validation {

// ArenaRetainedValue records copies and destructor calls while retaining the
// address established by each constructor. Managed container tests can then
// distinguish a valid copy into Arena storage from a byte copy and prove that
// replacement or reset leaves former Arena objects untouched.
class ArenaRetainedValue {
 public:
  struct Counts {
    Count copies = 0;
    Count destructions = 0;
  };

  ArenaRetainedValue(Count value, Counts& counts)
      : value(value), counts(&counts) {}

  ArenaRetainedValue(const ArenaRetainedValue& source)
      : value(source.value), counts(source.counts) {
    counts->copies++;
  }

  auto operator=(const ArenaRetainedValue&) -> ArenaRetainedValue& = delete;

  ~ArenaRetainedValue() { counts->destructions++; }

  constexpr auto get_value() -> Count& { return value; }
  constexpr auto get_value() const -> Count { return value; }
  constexpr auto has_valid_location() const -> Bool { return location == this; }

  constexpr auto hash() const -> U64 {
    return Core::Algorithm::Hash(value).get_value();
  }

  constexpr auto operator==(const ArenaRetainedValue& rhs) const -> Bool {
    return value == rhs.value;
  }

 private:
  Count value;
  Counts* counts;
  const ArenaRetainedValue* const location = this;
};

}  // namespace Perimortem::Validation
