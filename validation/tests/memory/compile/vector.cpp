// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/compile/vector.hpp"

#include "perimortem/memory/compile/object.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;

template <typename type>
static constexpr bool supports_forgetful_resize =
    requires(Compile::Vector<type>& values) { values.forgetful_resize(1); };
static_assert(supports_forgetful_resize<S32>);
static_assert([] {
  Compile::Vector<S32> values;
  values.insert(1);
  values.forgetful_resize(0);
  return values.is_empty() && !values.get_data() && !values.get_capacity();
}());

// Growth may move Object records but must leave their separate pointee
// allocations intact. Schema compilation retains exactly this kind of
// reference.
static_assert(
    [] {
      Compile::Vector<Compile::Object<S32>> values;

      const S32* retained = values.insert(42).get_data();
      for (S32 i = 0; i < 16; ++i) {
        values.insert(i);
      }

      return values.get_data()[0].get_data() == retained && *retained == 42;
    }(),

    "Vector growth moves its values without duplicating their ownership");

// Removal must leave a live, reusable array slot because delete[] will later
// destroy every capacity slot, including those outside the current size.
static_assert(
    [] {
      Compile::Vector<Compile::Object<S32>> values;
      values.insert(10);
      values.insert(20);

      const S32* retained = values.insert(30).get_data();
      if (!values.remove(1) || values.get_data()[1].get_data() != retained) {
        return false;
      }

      values.insert(40);
      if (!values.remove_stable(0) ||
          values.get_data()[0].get_data() != retained) {
        return false;
      }

      return values.get_size() == 2 && *values.get_data()[0].get_data() == 30 &&
             *values.get_data()[1].get_data() == 40 && !values.remove(2) &&
             !values.remove_stable(2);
    }(),

    "Both removals preserve survivors and permit reuse without double "
    "destruction");

// A requested size can exceed one doubling. Shrinking and regrowing inside
// retained capacity must release old values and expose default new slots.
static_assert(
    [] {
      Compile::Vector<Compile::Object<S32>> values;
      values.insert(42);

      const auto* retained = values.get_data()[0].get_data();
      values.resize(100);

      values.get_data()[99] = Compile::Object<S32>(7);

      auto* storage = values.get_data();
      values.resize(1);
      values.resize(100);

      return values.get_capacity() >= 100 && values.get_data() == storage &&
             values.get_data()[0].get_data() == retained &&
             !values.get_data()[99].get_data();
    }(),

    "Resize covers large requests and does not resurrect truncated values");

// Retaining a subrange reuses the allocation and clips the requested count to
// the available suffix. Every capacity slot remains valid for final deletion.
static_assert(
    [] {
      Compile::Vector<S32> values;
      values.insert(10);
      values.insert(20);
      values.insert(30);
      values.insert(40);

      auto* storage = values.get_data();
      const Count capacity = values.get_capacity();
      values.retain(1, Count(-1));

      return values.get_data() == storage &&
             values.get_capacity() == capacity && values.get_size() == 3 &&
             values.get_data()[0] == 20 && values.get_data()[1] == 30 &&
             values.get_data()[2] == 40;
    }(),

    "Retain clips and reuses Compile storage");

static_assert(
    [] {
      Compile::Vector<Compile::Object<S32>> source;
      source.insert(42);

      auto* retained = source.get_data();
      Compile::Vector<Compile::Object<S32>> moved(Data::take(source));

      auto& self = moved;
      moved = Data::take(self);
      Compile::Vector<Compile::Object<S32>> destination;
      destination.insert(7);
      destination = Data::take(moved);
      source.insert(3);

      return destination.get_data() == retained &&
             *destination.get_data()[0].get_data() == 42 && !moved.get_data() &&
             !moved.get_size() && !moved.get_capacity() &&
             *source.get_data()[0].get_data() == 3;
    }(),

    "Vector moves transfer the buffer and leave reusable empty donors");
