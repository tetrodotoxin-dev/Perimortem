// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.hpp"

namespace Perimortem::Core::Math {

// Scalar provides mathematics over individual numeric values. Its constexpr
// ordering and modular operations preserve the input type. Integer magnitude
// and bit width make representation boundaries explicit. Runtime square root
// and rounding preserve R64 so conversion to a discrete domain remains a
// caller decision. Compiler intrinsics keep the real operations behind the
// same public numeric boundary on every target.
class Scalar {
 public:
  template <typename type>
  static constexpr auto min(type left, type right) -> type {
    return left < right ? left : right;
  }

  template <typename type>
  static constexpr auto max(type left, type right) -> type {
    return left > right ? left : right;
  }

  template <typename type>
  static constexpr auto clamp(type value, type min_value, type max_value)
      -> type {
    return max(min(value, max_value), min_value);
  }

  // Returns the equivalent value in [0, modulo). Modulo must be positive.
  template <typename type>
  static constexpr auto wrap(type value, type modulo) -> type {
    value %= modulo;
    return value < 0 ? value + modulo : value;
  }

  // Converting a negative value to a wide enough unsigned type wraps within the
  // unsigned range. Negative one becomes the maximum value, so subtracting
  // those bits from zero produces magnitude one. The same subtraction
  // represents the signed minimum without forming a positive value its signed
  // type cannot hold.
  template <typename unsigned_type, typename value_type>
  static constexpr auto magnitude(value_type value) -> unsigned_type {
    static_assert(__is_integral(value_type));
    static_assert(!__is_same(value_type, bool));
    static_assert(__is_integral(unsigned_type));
    static_assert(!__is_signed(unsigned_type));
    static_assert(!__is_same(unsigned_type, bool));
    static_assert(sizeof(unsigned_type) >= sizeof(value_type));

    unsigned_type bits = unsigned_type(value);
    if constexpr (__is_signed(value_type)) {
      return value < 0 ? unsigned_type(0) - bits : bits;
    } else {
      return bits;
    }
  }

  // Returns the number of bits required to represent `value`. Zero requires no
  // bits, while every other value includes its highest set bit.
  static constexpr auto bit_width(U64 value) -> U64 {
    return 64 - __builtin_clzg(value, S32(sizeof(U64) * 8));
  }

  static auto sqrt(R64 value) -> R64 { return __builtin_sqrt(value); }

  static auto floor(R64 value) -> R64 { return __builtin_floor(value); }

  static auto ceil(R64 value) -> R64 { return __builtin_ceil(value); }
};

}  // namespace Perimortem::Core::Math
