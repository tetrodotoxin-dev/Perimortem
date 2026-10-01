// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/scalar.hpp"

#include "toolchain/validation/unit_test.hpp"

using namespace Perimortem::Core;
using namespace Toolchain::Validation;

static Harness CoreScalar = {
  .name = "Core::Scalar",
};

VALIDATION_TEST(CoreScalar, integer_width_fit) {
  EXPECT(Scalar::is_representable(S64(-128), 1));
  EXPECT(Scalar::is_representable(S64(127), 1));
  EXPECT_NOT(Scalar::is_representable(S64(-129), 1));
  EXPECT_NOT(Scalar::is_representable(S64(128), 1));
  EXPECT(Scalar::is_representable(S64(-9223372036854775807LL - 1), 8));
  EXPECT(Scalar::is_representable(S64(9223372036854775807LL), 8));

  EXPECT(Scalar::is_representable(U64(0), 1));
  EXPECT(Scalar::is_representable(U64(255), 1));
  EXPECT_NOT(Scalar::is_representable(U64(256), 1));
  EXPECT(Scalar::is_representable(U64(-1), 8));

  EXPECT_NOT(Scalar::is_representable(S64(0), 0));
  EXPECT_NOT(Scalar::is_representable(S64(0), 9));
  EXPECT_NOT(Scalar::is_representable(U64(0), 0));
  EXPECT_NOT(Scalar::is_representable(U64(0), 9));
}
