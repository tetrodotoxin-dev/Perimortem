// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/matrix.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/storage/vector.hpp"
#include "perimortem/serialization/rgba8.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::Serialization;
using namespace Toolchain::Validation;

static Harness MemoryMatrix = {.name = "Memory::Matrix"};

VALIDATION_TEST(MemoryMatrix, empty_value) {
  const Count requests = Allocator::Bibliotheca::check_out_requests();
  Matrix<Rgba8> matrix;
  EXPECT_EQ(Allocator::Bibliotheca::check_out_requests(), requests);
  EXPECT(matrix.is_empty());
  EXPECT(matrix.get_view().get_values().is_empty());
}

// Projection transfers an exact length allocation without copying its values.
// Moving the result carries both storage and the Math::Domain that indexes it.
VALIDATION_TEST(MemoryMatrix, projects_owned_values) {
  Thread::Vector<Rgba8> values;
  values.insert(Rgba8::from_rgba(17, 34, 51, 68));

  const auto* data = values.get_data();
  const Count requests = Allocator::Bibliotheca::check_out_requests();
  auto projected = Matrix<Rgba8>::project(
      Math::Domain<2>(Math::Size<U32, 2>({{1, 1}})), Data::take(values));
  Bool succeeded = False;
  Matrix<Rgba8> moved = projected.visit(
      [&](Matrix<Rgba8>& matrix) {
        succeeded = True;
        return Matrix<Rgba8>(Data::take(matrix));
      },

      [](Matrix<Rgba8>::Failure) { return Matrix<Rgba8>(); });
  ASSERT(succeeded);
  EXPECT_EQ(Allocator::Bibliotheca::check_out_requests(), requests);
  EXPECT_EQ(moved.get_view().get_values().get_data(), data);
  EXPECT(values.get_view().is_empty());

  const auto& observed = moved;
  auto selected = observed.get(Math::Point<S64, 2>({{0, 0}}));
  ASSERT(selected);
  selected.visit(
      [&](const Rgba8& value) { EXPECT_EQ(value.red, 17); }, []() {});
  EXPECT_NOT(moved.get(Math::Point<S64, 2>({{1, 0}})));
}

VALIDATION_TEST(MemoryMatrix, zero_domain) {
  auto matrix = Matrix<Rgba8>::create(
      Math::Domain<2>(Math::Size<U32, 2>({{0, 4}})), View::Vector<Rgba8>());
  EXPECT(matrix.is_empty());
  EXPECT_EQ(matrix.get_row_count(), 4);
  EXPECT(matrix.get_view().get_values().is_empty());
}

VALIDATION_TEST(MemoryMatrix, mixed_boundary_access) {
  Thread::Vector<Rgba8> values;
  values.insert(Rgba8::from_rgb(1, 0, 0));
  values.insert(Rgba8::from_rgb(2, 0, 0));
  values.insert(Rgba8::from_rgb(3, 0, 0));

  Math::Domain<2, Math::Boundary::Wrap, Math::Boundary::Clip> domain(
      Math::Size<U32, 2>({{3, 1}}));
  auto matrix =
      Matrix<Rgba8, Math::Boundary::Wrap, Math::Boundary::Clip>::create(
          domain, values.get_view());
  auto wrapped = matrix.get(Math::Point<S64, 2>({{-1, 0}}));
  ASSERT(wrapped);
  wrapped.visit([&](const Rgba8& value) { EXPECT_EQ(value.red, 3); }, []() {});
  EXPECT_NOT(matrix.get(Math::Point<S64, 2>({{0, 1}})));
}

VALIDATION_TEST(MemoryMatrix, uniform_boundary_access) {
  Thread::Vector<Rgba8> values;
  values.insert(Rgba8::from_rgb(1, 0, 0));
  values.insert(Rgba8::from_rgb(2, 0, 0));
  values.insert(Rgba8::from_rgb(3, 0, 0));
  values.insert(Rgba8::from_rgb(4, 0, 0));

  auto matrix = Matrix<Rgba8, Math::Boundary::Wrap>::create(
      Math::Domain<2, Math::Boundary::Wrap>(Math::Size<U32, 2>({{2, 2}})),
      values.get_view());
  auto wrapped = matrix.get(Math::Point<S64, 2>({{-1, -1}}));
  ASSERT(wrapped);
  wrapped.visit([&](const Rgba8& value) { EXPECT_EQ(value.red, 4); }, []() {});
}

VALIDATION_TEST(MemoryMatrix, swaps_complete_values) {
  auto first = Matrix<Rgba8>::create(
      Math::Domain<2>(Math::Size<U32, 2>({{2, 3}})), View::Vector<Rgba8>());
  auto second = Matrix<Rgba8>::create(
      Math::Domain<2>(Math::Size<U32, 2>({{4, 5}})), View::Vector<Rgba8>());
  const auto* first_data = first.get_view().get_values().get_data();
  const auto* second_data = second.get_view().get_values().get_data();
  second = Data::take(first);
  EXPECT_EQ(second.get_view().get_values().get_data(), first_data);
  EXPECT_EQ(second.get_column_count(), 2);
  EXPECT_EQ(first.get_view().get_values().get_data(), second_data);
  EXPECT_EQ(first.get_column_count(), 4);
}

VALIDATION_TEST(MemoryMatrix, copies_and_pads) {
  Thread::Vector<Rgba8> values;
  values.insert(Rgba8::from_rgb(1, 2, 3));

  auto matrix = Matrix<Rgba8>::create(
      Math::Domain<2>(Math::Size<U32, 2>({{2, 1}})), values.get_view());
  EXPECT_EQ(values.get_size(), 1);
  EXPECT(matrix.get_view().get_values().get_data() != values.get_data());

  auto first = matrix.get(Math::Point<S64, 2>({{0, 0}}));
  auto padded = matrix.get(Math::Point<S64, 2>({{1, 0}}));
  ASSERT(first && padded);
  first.visit(
      [&](const Rgba8& value) { EXPECT_EQ(value.alpha, 255); }, []() {});
  padded.visit([&](const Rgba8& value) { EXPECT_EQ(value.alpha, 0); }, []() {});
  EXPECT_EQ(matrix.get_view().get_value_count(), 2);
}

VALIDATION_TEST(MemoryMatrix, pads_trivial_values) {
  Storage::Vector<U8, 1> source = {{7}};
  auto matrix = Matrix<U8>::create(
      Math::Domain<2>(Math::Size<U32, 2>({{2, 1}})), source.get_view());
  auto values = matrix.get_view().get_values();
  EXPECT_EQ(values.get_data()[0], 7);
  EXPECT_EQ(values.get_data()[1], 0);
}

VALIDATION_TEST(MemoryMatrix, publishes_shaped_access) {
  Storage::Vector<U8, 4> source = {{1, 2, 3, 4}};
  auto matrix = Matrix<U8>::create(
      Math::Domain<2>(Math::Size<U32, 2>({{2, 2}})), source.get_view());

  auto access = matrix.get_access();
  auto selected = access.get(Math::Point<S64, 2>({{1, 1}}));
  ASSERT(selected);
  selected.visit([](U8& value) { value = 9; }, []() {});

  auto observed = matrix.get_view().get(Math::Point<S64, 2>({{1, 1}}));
  ASSERT(observed);
  observed.visit([&](const U8& value) { EXPECT_EQ(value, 9); }, []() {});
}

VALIDATION_TEST(MemoryMatrix, truncates_source_view) {
  Thread::Vector<Rgba8> values;
  values.resize(32);

  values.get_data()[0] = Rgba8::from_rgb(1, 2, 3);

  const auto* data = values.get_data();
  auto matrix = Matrix<Rgba8>::create(
      Math::Domain<2>(Math::Size<U32, 2>({{1, 1}})), values.get_view());
  EXPECT_EQ(values.get_size(), 32);
  EXPECT(matrix.get_view().get_values().get_data() != data);
  EXPECT_EQ(matrix.get_view().get_value_count(), 1);
  EXPECT_EQ(matrix.get_view().get_values().get_data()[0].red, 1);
}

VALIDATION_TEST(MemoryMatrix, bad_projection_dimensions) {
  Thread::Vector<Rgba8> values;
  values.insert(Rgba8::from_rgb(1, 2, 3));

  const auto* data = values.get_data();
  auto projected = Matrix<Rgba8>::project(
      Math::Domain<2>(Math::Size<U32, 2>({{2, 1}})), Data::take(values));
  Bool rejected = False;
  projected.visit(
      [](Matrix<Rgba8>&) {},
      [&](Matrix<Rgba8>::Failure failure) {
        rejected = failure == Matrix<Rgba8>::Failure::BadDimensions;
      });

  EXPECT(rejected);
  EXPECT_EQ(values.get_data(), data);
  EXPECT_EQ(values.get_size(), 1);
}
