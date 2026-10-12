// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/const/vector.hpp"

#include "perimortem/memory/const/object.hpp"

using namespace Perimortem::Memory;

template <typename type>
static constexpr bool supports_forgetful_resize =
    requires(Const::Vector<type>& values) { values.forgetful_resize(1); };
static_assert(!supports_forgetful_resize<S32>);

// Growth may move the owner records but must leave their separately allocated
// pointees intact. Schema compilation retains exactly this kind of reference.
static_assert(
    [] {
      Const::Vector<Const::Object<S32>> values;

      const S32* retained = values.insert(42).get_data();
      for (S32 i = 0; i < 16; ++i) {
        values.insert(i);
      }

      return values[0].get_data() == retained && *retained == 42;
    }(),
    "Vector growth moves its elements without duplicating their ownership");

// Removal must leave a live, reusable array slot because delete[] will later
// destroy every capacity slot, including those outside the logical size.
static_assert(
    [] {
      Const::Vector<Const::Object<S32>> values;
      values.insert(10);
      values.insert(20);

      const S32* retained = values.insert(30).get_data();
      if (!values.remove(1) || values[1].get_data() != retained) {
        return false;
      }

      values.insert(40);
      if (!values.remove_stable(0) || values[0].get_data() != retained) {
        return false;
      }

      return values.get_size() == 2 && *values[0].get_data() == 30 &&
             *values[1].get_data() == 40 && !values.remove(2) &&
             !values.remove_stable(2);
    }(),
    "Both removals preserve survivors and permit reuse without double "
    "destruction");

// A requested size can exceed one doubling. Shrinking and regrowing inside
// retained capacity must release old values and expose default new slots.
static_assert(
    [] {
      Const::Vector<Const::Object<S32>> values;
      values.insert(42);

      const auto* retained = values[0].get_data();
      values.resize(100);

      values[99] = Const::Object<S32>(7);

      auto* storage = values.get_data();
      values.resize(1);
      values.resize(100);

      return values.get_capacity() >= 100 && values.get_data() == storage &&
             values[0].get_data() == retained && !values[99].get_data();
    }(),
    "Resize covers large requests and does not resurrect truncated values");

// Retaining a subrange reuses the allocation and clips the requested count to
// the available suffix. Every capacity slot remains valid for final deletion.
static_assert(
    [] {
      Const::Vector<S32> values;
      values.insert(10);
      values.insert(20);
      values.insert(30);
      values.insert(40);

      auto* storage = values.get_data();
      const Count capacity = values.get_capacity();
      values.retain(1, Count(-1));

      return values.get_data() == storage &&
             values.get_capacity() == capacity && values.get_size() == 3 &&
             values[0] == 20 && values[1] == 30 && values[2] == 40;
    }(),
    "Retain clips and reuses Const storage");

static_assert(
    [] {
      Const::Vector<Const::Object<S32>> source;
      source.insert(42);

      auto* retained = source.get_data();
      Const::Vector<Const::Object<S32>> moved(
          static_cast<Const::Vector<Const::Object<S32>>&&>(source));

      auto& self = moved;
      moved = static_cast<Const::Vector<Const::Object<S32>>&&>(self);
      Const::Vector<Const::Object<S32>> destination;
      destination.insert(7);
      destination = static_cast<Const::Vector<Const::Object<S32>>&&>(moved);
      source.insert(3);

      return destination.get_data() == retained &&
             *destination[0].get_data() == 42 && !moved.get_data() &&
             !moved.get_size() && !moved.get_capacity() &&
             *source[0].get_data() == 3;
    }(),
    "Vector moves transfer the buffer and leave reusable empty donors");
