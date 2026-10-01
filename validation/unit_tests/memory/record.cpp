// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/dynamic/record.hpp"

#include "perimortem/core/null_terminated.hpp"

#include "perimortem/memory/dynamic/map.hpp"

#include "toolchain/validation/unit_test.hpp"

using namespace Perimortem::Memory;
using namespace Toolchain::Validation;

static Harness DynamicRecord = {
  .name = "Dynamic::Record",
};

class RaiiProbe {
 public:
  RaiiProbe(Count& destructor_count, Count value = 0)
      : destructor_count(destructor_count), value(value) {}

  ~RaiiProbe() { destructor_count++; }

  constexpr auto get_value() const -> Count { return value; }

 private:
  Count& destructor_count;
  Count value = 0;
};

VALIDATION_TEST(DynamicRecord, shared_lifetime) {
  Count destructor_count = 0;

  {
    Dynamic::Record<RaiiProbe> probe(destructor_count, 42);
    EXPECT_EQ(probe->get_value(), Count(42));
    EXPECT_EQ(probe.get_reservations(), Count(1));

    {
      Dynamic::Record<RaiiProbe> second = probe;
      EXPECT_EQ(probe.get_reservations(), Count(2));
      EXPECT_EQ(second->get_value(), Count(42));
      EXPECT_EQ(destructor_count, Count(0));
    }

    EXPECT_EQ(destructor_count, Count(0));
    EXPECT_EQ(probe.get_reservations(), Count(1));
  }

  EXPECT_EQ(destructor_count, Count(1));
}

VALIDATION_TEST(DynamicRecord, assignment) {
  Count destructor_count = 0;

  {
    Dynamic::Record<RaiiProbe> first(destructor_count, 1);
    Dynamic::Record<RaiiProbe> second(destructor_count, 2);

    second = first;
    EXPECT_EQ(destructor_count, Count(1));
    EXPECT_EQ(second->get_value(), Count(1));
  }

  EXPECT_EQ(destructor_count, Count(2));
}

VALIDATION_TEST(DynamicRecord, assignment_reserves) {
  Count destructor_count = 0;

  {
    Dynamic::Record<RaiiProbe> first(destructor_count, 3);
    Dynamic::Record<RaiiProbe> second = first;
    const Dynamic::Record<RaiiProbe>& alias = first;

    first = alias;
    second = first;
    EXPECT_EQ(destructor_count, Count(0));
  }

  EXPECT_EQ(destructor_count, Count(1));
}

VALIDATION_TEST(DynamicRecord, move_assignment) {
  Count destructor_count = 0;

  {
    Dynamic::Record<RaiiProbe> first(destructor_count, 1);
    {
      Dynamic::Record<RaiiProbe> second(destructor_count, 2);

      first = static_cast<Dynamic::Record<RaiiProbe>&&>(second);
      EXPECT_EQ(first->get_value(), Count(2));
      EXPECT_EQ(destructor_count, Count(0));
    }

    EXPECT_EQ(first->get_value(), Count(2));
    EXPECT_EQ(destructor_count, Count(1));
  }

  EXPECT_EQ(destructor_count, Count(2));
}

VALIDATION_TEST(DynamicRecord, move_construction) {
  Count destructor_count = 0;

  {
    Dynamic::Record<RaiiProbe> first(destructor_count, 3);
    {
      Dynamic::Record<RaiiProbe> second(
          static_cast<Dynamic::Record<RaiiProbe>&&>(first));
      EXPECT_EQ(second->get_value(), Count(3));
      EXPECT_EQ(first->get_value(), Count(3));
      EXPECT_EQ(first.get_reservations(), Count(2));
    }

    EXPECT_EQ(destructor_count, Count(0));
  }

  EXPECT_EQ(destructor_count, Count(1));
}

VALIDATION_TEST(DynamicRecord, map_owner) {
  Count destructor_count = 0;
  Dynamic::Map<Count, Dynamic::Record<RaiiProbe>> values;

  {
    Dynamic::Record<RaiiProbe> probe(destructor_count, 7);
    values.insert(0, probe);
  }

  EXPECT_EQ(destructor_count, Count(0));
  auto found = values.find(0);
  ASSERT(found);
  EXPECT_EQ((*found).value->get_value(), Count(7));

  values.remove(0);
  EXPECT_EQ(destructor_count, Count(1));
}

VALIDATION_TEST(DynamicRecord, map_rehash) {
  Count destructor_count = 0;
  Dynamic::Map<Count, Dynamic::Record<RaiiProbe>> values;
  for (Count i = 0; i < 16; i++) {
    Dynamic::Record<RaiiProbe> probe(destructor_count, i);
    values.insert(i, probe);
  }

  EXPECT_EQ(destructor_count, Count(0));
  for (Count i = 0; i < 16; i++) {
    auto found = values.find(i);
    ASSERT(found);
    EXPECT_EQ((*found).value->get_value(), i);
  }

  values.clear();
  EXPECT_EQ(destructor_count, Count(16));
}
