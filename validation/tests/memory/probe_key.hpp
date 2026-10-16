// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/algorithm/hash.hpp"

namespace Perimortem::Validation {

// ProbeKey separates bucket selection from key identity. Tests can place
// several identities at one home bucket or build a chosen sequence of home
// buckets without depending on the numeric hash implementation.
class ProbeKey {
 public:
  constexpr ProbeKey(Count home, Count identity)
      : home(home), identity(identity) {}

  constexpr auto hash() const -> U64 { return home; }
  constexpr auto operator==(const ProbeKey& rhs) const -> Bool {
    return home == rhs.home && identity == rhs.identity;
  }

 private:
  Count home;
  Count identity;
};

}  // namespace Perimortem::Validation
