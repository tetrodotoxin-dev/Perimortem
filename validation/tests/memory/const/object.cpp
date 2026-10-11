// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/const/object.hpp"

using namespace Perimortem::Memory;

// These owners exist during constant evaluation. Keeping the checks there
// makes a leak, dangling access or repeated destruction a compiler error.
static_assert(
    [] {
      Const::Object<S32> empty;
      Const::Object<S32> copy(empty);
      Const::Object<S32> replaced(7);
      replaced = empty;
      return !copy.get_data() && !replaced.get_data();
    }(),
    "Copying an empty Object preserves absence");

static_assert(
    [] {
      Const::Object<S32> source(42);
      Const::Object<S32> copy(source);
      *copy.get_data() = 7;

      auto& self = source;
      source = self;
      source = static_cast<Const::Object<S32>&&>(self);
      return *source.get_data() == 42 && *copy.get_data() == 7;
    }(),
    "Copies are independent and self assignment preserves ownership");

// Compiled records can point into these allocations. Moving an owner must
// transfer that allocation, rather than copy its value into a new one.
static_assert(
    [] {
      Const::Object<S32> source(42);
      const S32* retained = source.get_data();
      Const::Object<S32> moved(static_cast<Const::Object<S32>&&>(source));
      Const::Object<S32> destination(7);
      destination = static_cast<Const::Object<S32>&&>(moved);
      moved = Const::Object<S32>(9);
      source = Const::Object<S32>(3);
      return destination.get_data() == retained && *retained == 42 &&
             *moved.get_data() == 9 && *source.get_data() == 3;
    }(),
    "Moving an Object preserves its pointee and leaves a reusable donor");
