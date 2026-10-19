// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/access/matrix.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/math/point.hpp"
#include "perimortem/core/math/size.hpp"
#include "perimortem/core/storage/matrix.hpp"
#include "perimortem/core/view/matrix.hpp"

using namespace Perimortem::Core;
using namespace Toolchain::Validation;

static Harness CoreMatrix = {
  .name = "Core::Matrix",
};

VALIDATION_TEST(CoreMatrix, exact_shape) {
  const U32 values[] = {1, 2, 3, 4, 5, 6};
  View::Matrix<U32, 2> matrix(
      Math::Size<U32, 2>({{3, 2}}), View::Vector<U32>(values));

  EXPECT_EQ(matrix.get_extent(0), 3);
  EXPECT_EQ(matrix.get_extent(1), 2);
  EXPECT_EQ(matrix.get_value_count(), 6);

  auto selected = matrix.get(Math::Point<S64, 2>({{1, 1}}));
  ASSERT(selected);
  selected.visit([&](const U32& value) { EXPECT_EQ(value, 5); }, []() {});

  EXPECT_NOT(matrix.get(Math::Point<S64, 2>({{-1, 0}})));
  EXPECT_NOT(matrix.get(Math::Point<S64, 2>({{3, 0}})));
  EXPECT_NOT(matrix.get(Math::Point<S64, 2>({{0, 2}})));
}

VALIDATION_TEST(CoreMatrix, three_dimensions) {
  const U32 values[] = {0, 1, 2, 3, 4, 5, 6, 7};
  View::Matrix<U32, 3> matrix(
      Math::Size<U32, 3>({{2, 2, 2}}), View::Vector<U32>(values));

  auto selected = matrix.get(Math::Point<S64, 3>({{1, 0, 1}}));
  ASSERT(selected);
  selected.visit([&](const U32& value) { EXPECT_EQ(value, 5); }, []() {});
}

VALIDATION_TEST(CoreMatrix, invalid_shape) {
  const U32 values[] = {1, 2, 3};
  View::Matrix<U32, 2> mismatched(
      Math::Size<U32, 2>({{2, 2}}), View::Vector<U32>(values));
  EXPECT(mismatched.is_empty());
  EXPECT_EQ(mismatched.get_extent(0), 0);
  EXPECT_EQ(mismatched.get_value_count(), 0);

  View::Matrix<U32, 3> overflow(
      Math::Size<U32, 3>({{~U32{}, ~U32{}, ~U32{}}}), View::Vector<U32>());
  EXPECT(overflow.is_empty());
  EXPECT_EQ(overflow.get_extent(0), 0);

  U32 writable_values[] = {1, 2, 3};
  auto writable_mismatch = Access::Matrix<U32, 2>::create(
      Math::Size<U32, 2>({{2, 2}}), Access::Vector<U32>(writable_values));
  EXPECT_NOT(writable_mismatch);
}

VALIDATION_TEST(CoreMatrix, empty_axis) {
  View::Matrix<U32, 2> matrix(
      Math::Size<U32, 2>({{0, 4}}), View::Vector<U32>());
  EXPECT(matrix.is_empty());
  EXPECT_EQ(matrix.get_extent(0), 0);
  EXPECT_EQ(matrix.get_extent(1), 4);
  EXPECT_EQ(matrix.get_value_count(), 0);

  U32 writable_anchor = 0;
  auto writable = Access::Matrix<U32, 2>::create(
      Math::Size<U32, 2>({{0, 4}}), Access::Vector<U32>(writable_anchor, 0));
  ASSERT(writable);
  writable.visit(
      [&](Access::Matrix<U32, 2>& access) {
        EXPECT(access.is_empty());
        EXPECT_EQ(access.get_extent(0), 0);
        EXPECT_EQ(access.get_extent(1), 4);
        EXPECT_EQ(access.get_value_count(), 0);
      },

      []() {});
}

VALIDATION_TEST(CoreMatrix, writable_access) {
  U32 values[] = {1, 2, 3, 4};
  auto matrix = Access::Matrix<U32, 2>::create(
      Math::Size<U32, 2>({{2, 2}}), Access::Vector<U32>(values));
  ASSERT(matrix);
  matrix.visit(
      [&](Access::Matrix<U32, 2>& access) {
        auto selected = access.get(Math::Point<S64, 2>({{0, 1}}));
        ASSERT(selected);
        selected.visit([](U32& value) { value = 9; }, []() {});
      },

      []() {});
  EXPECT_EQ(values[2], 9);

  matrix.visit(
      [&](const Access::Matrix<U32, 2>& access) {
        View::Matrix<U32, 2> view = access;
        auto observed = view.get(Math::Point<S64, 2>({{0, 1}}));
        ASSERT(observed);
        observed.visit([&](const U32& value) { EXPECT_EQ(value, 9); }, []() {});
      },

      []() {});
}

VALIDATION_TEST(CoreMatrix, embedded_storage) {
  Storage::Matrix<U32, 3, 2> matrix = {{1, 2, 3, 4, 5, 6}};

  EXPECT_EQ(matrix.get_extent(0), 3);
  EXPECT_EQ(matrix.get_extent(1), 2);
  EXPECT_EQ(matrix.get_value_count(), 6);

  Access::Matrix<U32, 2> access = matrix;
  auto selected = access.get(Math::Point<S64, 2>({{1, 1}}));
  ASSERT(selected);
  selected.visit([](U32& value) { value = 9; }, []() {});

  View::Matrix<U32, 2> view = matrix;
  auto observed = view.get(Math::Point<S64, 2>({{1, 1}}));
  ASSERT(observed);
  observed.visit([&](const U32& value) { EXPECT_EQ(value, 9); }, []() {});
}

VALIDATION_TEST(CoreMatrix, embedded_default_values) {
  constexpr Storage::Matrix<U32, 2, 2> matrix;
  static_assert(matrix.get_value_count() == 4);

  auto selected = matrix.get(Math::Point<S64, 2>({{1, 1}}));
  ASSERT(selected);
  selected.visit([&](const U32& value) { EXPECT_EQ(value, 0); }, []() {});
}
