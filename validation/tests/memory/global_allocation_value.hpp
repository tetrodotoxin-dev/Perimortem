// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/algorithm/hash.hpp"
#include "perimortem/core/data.hpp"
#include "perimortem/memory/allocator/global.hpp"

namespace Perimortem::Validation {

// GlobalAllocationValue gives container tests a resource whose allocation can
// cross thread boundaries. Copying duplicates the resource, moving transfers
// it, and destruction returns it through the Global allocator. The counters
// distinguish those operations while the stored address detects byte copies
// that bypass construction.
class GlobalAllocationValue {
 public:
  struct Counts {
    Count copies = 0;
    Count moves = 0;
    Count releases = 0;
  };

  // Explicit construction keeps the process allocation visible when a tested
  // operation also accepts the scalar payload.
  explicit GlobalAllocationValue(U32 value, Counts& counts)
      : value(allocate(value)), counts(&counts) {}

  GlobalAllocationValue(const GlobalAllocationValue& source)
      : value(source.value ? allocate(*source.value) : nullptr),
        counts(source.counts) {
    counts->copies++;
  }

  GlobalAllocationValue(GlobalAllocationValue&& source)
      : counts(source.counts) {
    counts->moves++;
    Core::Data::swap(value, source.value);
  }

  auto operator=(const GlobalAllocationValue&)
      -> GlobalAllocationValue& = delete;
  auto operator=(GlobalAllocationValue&&) -> GlobalAllocationValue& = delete;

  ~GlobalAllocationValue() {
    if (value) {
      Memory::Allocator::Global::release(Core::Data::cast<U8>(value));
      counts->releases++;
    }
  }

  auto get_value() const -> U32 { return *value; }
  auto get_data() const -> const U32* { return value; }
  auto has_valid_location() const -> Bool { return location == this; }
  auto hash() const -> U64 { return Core::Algorithm::Hash(*value).get_value(); }
  auto operator==(const GlobalAllocationValue& rhs) const -> Bool {
    return *value == *rhs.value;
  }

 private:
  static auto allocate(U32 source) -> U32* {
    auto allocation = Memory::Allocator::Global::allocate(sizeof(U32));
    U32* result = Core::Data::cast<U32>(allocation.get_data());
    *result = source;
    return result;
  }

  U32* value = nullptr;
  Counts* counts;
  const GlobalAllocationValue* const location = this;
};

}  // namespace Perimortem::Validation
