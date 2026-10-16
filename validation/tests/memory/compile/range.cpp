// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/compile/range.hpp"

#include "perimortem/memory/compile/object.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;

// A const observation must use the same allocation and extent as mutable
// indexing without making Range mutable to read a value.
static_assert(
    [] {
      Compile::Range<S32> source(2);
      source[1] = 42;

      const auto& view = source;
      return view[1] == 42 && view.get_size() == 2;
    }(),

    "Range provides both const and mutable indexed access");

static_assert(
    [] {
      Compile::Range<S32> source(2);
      source.get_data()[0] = 42;

      Compile::Range<S32> copy(source);
      copy.get_data()[0] = 7;

      auto& self = source;
      source = self;
      source = Data::take(self);

      Compile::Range<S32> destination(8);
      destination = copy;
      return destination.get_size() == 2 && destination.get_data()[0] == 7 &&
             source.get_size() == 2 && source.get_data()[0] == 42;
    }(),

    "Range copies and self assignment preserve the selected values");

// Position tables retain pointers into their Range. Moving it must preserve
// those pointers, and replacing it with an empty Range must release the former
// allocation.
static_assert(
    [] {
      Compile::Range<Compile::Object<S32>> source(2);
      source.get_data()[1] = Compile::Object<S32>(42);

      const auto* retained = source.get_data();
      Compile::Range<Compile::Object<S32>> moved(Data::take(source));
      Compile::Range<Compile::Object<S32>> destination;
      destination = Data::take(moved);

      const bool preserved =
          destination.get_data() == retained && *retained[1].get_data() == 42;
      destination = Compile::Range<Compile::Object<S32>>();
      return preserved && !source.get_size() && !source.get_data() &&
             !moved.get_size() && !moved.get_data() && !destination.get_size();
    }(),

    "Range moves preserve the array and empty assignment retires it");
