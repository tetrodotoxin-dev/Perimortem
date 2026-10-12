// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/test.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/memory/global/vector.hpp"
#include "perimortem/system/process/worker.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;
using namespace Toolchain::Validation;

static_assert(__is_constructible(Global::Vector<U32>));
template <typename type>
static constexpr bool supports_forgetful_resize =
    requires(Global::Vector<type>& values) { values.forgetful_resize(1); };
static_assert(!supports_forgetful_resize<U32>);
static_assert(
    sizeof(Global::Vector<U32>) ==
    Data::align<alignof(Count)>(sizeof(U32*)) + sizeof(Count) * 2);
static_assert([] {
  Global::Vector<U32> values;
  return values.is_empty() && values.get_capacity() == 0 &&
         values.get_data() == nullptr;
}());

static Harness GlobalVector = {
  .name = "Global::Vector",
};

static Global::Vector<U32> transferred_values;

class GlobalOwner {
 public:
  explicit GlobalOwner(U32 value) : value(allocate(value)) {}
  GlobalOwner(const GlobalOwner& other)
      : value(other.value ? allocate(*other.value) : nullptr) {
    copies++;
  }
  GlobalOwner(GlobalOwner&& other) {
    moves++;
    Data::swap(value, other.value);
  }
  ~GlobalOwner() {
    if (value) {
      Allocator::Global::release(Data::cast<U8>(value));
      releases++;
    }
  }

  static auto reset_counts() -> void {
    copies = 0;
    moves = 0;
    releases = 0;
  }

  static auto get_copies() -> Count { return copies; }
  static auto get_moves() -> Count { return moves; }
  static auto get_releases() -> Count { return releases; }

  auto get_value() const -> U32 { return *value; }
  auto has_valid_location() const -> Bool { return location == this; }

 private:
  static auto allocate(U32 value) -> U32* {
    auto allocation = Allocator::Global::allocate(sizeof(U32));
    U32* result = Data::cast<U32>(allocation.get_data());
    *result = value;
    return result;
  }

  U32* value = nullptr;
  const GlobalOwner* const location = this;
  inline static Count copies = 0;
  inline static Count moves = 0;
  inline static Count releases = 0;
};

static Global::Vector<GlobalOwner> transferred_owners;

static auto create_global_vector(View::Bytes) -> void {
  Global::Vector<U32> created;
  created.insert(4);
  created.insert(8);
  transferred_values = static_cast<Global::Vector<U32>&&>(created);
}

static auto create_global_owners(View::Bytes) -> void {
  Global::Vector<GlobalOwner> created;
  for (U32 value = 0; value < 128; value++) {
    created.emplace(GlobalOwner(value));
  }

  transferred_owners = static_cast<Global::Vector<GlobalOwner>&&>(created);
}

VALIDATION_TEST(GlobalVector, value_semantics) {
  Global::Vector<U32> original;
  original.insert(4);
  original.insert(8);

  Global::Vector<U32> copied(original);
  copied.insert(16);

  EXPECT_EQ(original.get_size(), Count(2));
  EXPECT_EQ(copied.get_size(), Count(3));
  EXPECT(copied.get_data() != original.get_data());
  EXPECT_EQ(copied[2], U32(16));
}

VALIDATION_TEST(GlobalVector, cross_thread_transfer) {
  auto worker = Process::Worker::start(
      "global.vector"_view, create_global_vector, View::Bytes());
  worker.join();

  EXPECT_EQ(transferred_values.get_size(), Count(2));
  EXPECT_EQ(transferred_values[0], U32(4));
  EXPECT_EQ(transferred_values[1], U32(8));
  transferred_values.insert(16);
  EXPECT_EQ(transferred_values[2], U32(16));

  // Main thread release proves that storage ownership is independent of the
  // thread that populated the vector.
  transferred_values.reset();
  EXPECT(!transferred_values.get_data());
}

// Reallocation consumes the old process heap objects rather than copying them.
// Releasing the completed vector on this thread proves that both its element
// resources and backing allocation can cross the worker boundary.
VALIDATION_TEST(GlobalVector, owning_cross_thread_reallocation) {
  GlobalOwner::reset_counts();

  auto worker = Process::Worker::start(
      "global.owners"_view, create_global_owners, View::Bytes());
  worker.join();

  EXPECT_EQ(transferred_owners.get_size(), Count(128));
  EXPECT_EQ(GlobalOwner::get_copies(), Count(0));
  EXPECT(GlobalOwner::get_moves() > transferred_owners.get_size());
  for (Count index = 0; index < transferred_owners.get_size(); index++) {
    EXPECT_EQ(transferred_owners[index].get_value(), U32(index));
    EXPECT(transferred_owners[index].has_valid_location());
  }

  EXPECT(transferred_owners.remove(0));
  EXPECT(transferred_owners.remove_stable(0));
  EXPECT_EQ(GlobalOwner::get_copies(), Count(0));

  transferred_owners.reset();
  EXPECT_EQ(GlobalOwner::get_releases(), Count(128));
}
