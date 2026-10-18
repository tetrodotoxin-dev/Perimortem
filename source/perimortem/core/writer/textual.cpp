// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/writer/textual.hpp"

#include "perimortem/core/math/scalar.hpp"
#include "perimortem/core/view/null_terminated.hpp"

using namespace Perimortem::Core;

template <typename text_buffer>
constexpr auto
    write_text(Access::Bytes& target, Count& cursor, const text_buffer& text)
        -> Bool {
  if (cursor > target.get_size() ||
      text.get_size() > target.get_size() - cursor) {
    return false;
  }

  auto data = target.get_data();
  Data::copy(data + cursor, text.get_data(), text.get_size());
  cursor += text.get_size();
  return true;
}

constexpr auto create_digit_table() -> Storage::Bytes<200> {
  Storage::Bytes<200> values;
  for (Count d1 = 0; d1 < 10; d1++) {
    for (Count d2 = 0; d2 < 10; d2++) {
      auto location = (d2 * 10 + d1) * 2;
      values[location + 0] = '0' + d2;
      values[location + 1] = '0' + d1;
    }
  }

  return values;
}

constexpr auto digit_buffer = create_digit_table();

template <typename storage_type, typename unsigned_type>
static constexpr auto decimal_length(storage_type value) -> Count {
  Count sign = 0;

  if constexpr (__is_signed(storage_type)) {
    if (value < storage_type(0)) {
      sign = 1;
    }
  }

  unsigned_type magnitude = Math::Scalar::magnitude<unsigned_type>(value);

  // Local x86-64 release measurements found the comparison ladder about 20%
  // faster than repeatedly dividing the value to derive its decimal length.
  if (magnitude < 10ULL) {
    return sign + 1;
  } else if (magnitude < 100ULL) {
    return sign + 2;
  } else if (magnitude < 1'000ULL) {
    return sign + 3;
  } else if (magnitude < 10'000ULL) {
    return sign + 4;
  } else if (magnitude < 100'000ULL) {
    return sign + 5;
  } else if (magnitude < 1'000'000ULL) {
    return sign + 6;
  } else if (magnitude < 10'000'000ULL) {
    return sign + 7;
  } else if (magnitude < 100'000'000ULL) {
    return sign + 8;
  } else if (magnitude < 1'000'000'000ULL) {
    return sign + 9;
  } else if (magnitude < 10'000'000'000ULL) {
    return sign + 10;
  } else if (magnitude < 100'000'000'000ULL) {
    return sign + 11;
  } else if (magnitude < 1'000'000'000'000ULL) {
    return sign + 12;
  } else if (magnitude < 10'000'000'000'000ULL) {
    return sign + 13;
  } else if (magnitude < 100'000'000'000'000ULL) {
    return sign + 14;
  } else if (magnitude < 1'000'000'000'000'000ULL) {
    return sign + 15;
  } else if (magnitude < 10'000'000'000'000'000ULL) {
    return sign + 16;
  } else if (magnitude < 100'000'000'000'000'000ULL) {
    return sign + 17;
  } else if (magnitude < 1'000'000'000'000'000'000ULL) {
    return sign + 18;
  } else if (magnitude < 10'000'000'000'000'000'000ULL) {
    return sign + 19;
  }

  return sign + 20;
}

// The caller supplies the length so digits can fill backwards from their final
// offset without a scratch copy.
template <typename value_type, typename storage_type>
constexpr auto write_decimal(
    Access::Bytes& target,
    Count& cursor,
    value_type value,
    Count length) -> Bool {
  if (cursor > target.get_size() || length > target.get_size() - cursor)
      [[unlikely]] {
    return false;
  }

  auto data = target.get_data();
  if (value == 0) {
    data[cursor++] = '0';
    return true;
  }

  U8* output_digits = data + cursor;
  Count digits = length;
  if constexpr (__is_signed(value_type)) {
    if (value < value_type(0)) {
      *output_digits++ = '-';
      digits--;
    }
  }

  storage_type magnitude = Math::Scalar::magnitude<storage_type>(value);

  Count digit_position = digits - 1;
  while (magnitude >= 10) {
    auto two_digits = Count(magnitude % 100) << 1;
    magnitude /= 100;
    output_digits[digit_position] = digit_buffer.get_data()[two_digits + 1];
    output_digits[digit_position - 1] = digit_buffer.get_data()[two_digits];
    digit_position -= 2;
  }

  if (magnitude > 0) {
    output_digits[0] = '0' + U8(magnitude);
  }

  cursor += length;
  return true;
}

auto Writer::Textual::set_pointer(Count location) -> void {
  cursor = location < source.get_size() ? location : source.get_size();
}

auto Writer::Textual::operator<<(char character) -> Writer::Textual& {
  valid_state = valid_state && cursor < source.get_size();
  if (valid_state) {
    auto* data = source.get_data();
    data[cursor++] = U8(character);
  }

  return *this;
}

auto Writer::Textual::operator<<(const Bool flag) -> Writer::Textual& {
  if (flag) {
    valid_state = valid_state && write_text(source, cursor, "true"_view);
  } else {
    valid_state = valid_state && write_text(source, cursor, "false"_view);
  }

  return *this;
}

auto Writer::Textual::operator<<(const U8 value) -> Writer::Textual& {
  if (valid_state) {
    valid_state = write_decimal<U8, U8>(
        source, cursor, value, decimal_length<U8, U8>(value));
  }

  return *this;
}

auto Writer::Textual::operator<<(const U16 value) -> Writer::Textual& {
  if (valid_state) {
    valid_state = write_decimal<U16, U16>(
        source, cursor, value, decimal_length<U16, U16>(value));
  }

  return *this;
}

auto Writer::Textual::operator<<(const U32 value) -> Writer::Textual& {
  if (valid_state) {
    valid_state = write_decimal<U32, U32>(
        source, cursor, value, decimal_length<U32, U32>(value));
  }

  return *this;
}

auto Writer::Textual::operator<<(const U64 value) -> Writer::Textual& {
  if (valid_state) {
    valid_state = write_decimal<U64, U64>(
        source, cursor, value, decimal_length<U64, U64>(value));
  }

  return *this;
}

auto Writer::Textual::operator<<(const S8 value) -> Writer::Textual& {
  if (valid_state) {
    valid_state = write_decimal<S8, U8>(
        source, cursor, value, decimal_length<S8, U8>(value));
  }

  return *this;
}

auto Writer::Textual::operator<<(const S16 value) -> Writer::Textual& {
  if (valid_state) {
    valid_state = write_decimal<S16, U16>(
        source, cursor, value, decimal_length<S16, U16>(value));
  }

  return *this;
}

auto Writer::Textual::operator<<(const S32 value) -> Writer::Textual& {
  if (valid_state) {
    valid_state = write_decimal<S32, U32>(
        source, cursor, value, decimal_length<S32, U32>(value));
  }

  return *this;
}

auto Writer::Textual::operator<<(const S64 value) -> Writer::Textual& {
  if (valid_state) {
    valid_state = write_decimal<S64, U64>(
        source, cursor, value, decimal_length<S64, U64>(value));
  }

  return *this;
}

auto Writer::Textual::operator<<(const R32 r32) -> Writer::Textual& {
  write_real(R64(r32), __FLT_EPSILON__);
  return *this;
}

auto Writer::Textual::operator<<(const R64 r64) -> Writer::Textual& {
  write_real(r64, __DBL_EPSILON__);
  return *this;
}

// Fixed notation avoids locale state and runtime allocation. The supplied type
// precision bounds the fractional loop, while `max_length` prevents a long
// binary fraction from consuming the remaining output buffer.
auto Writer::Textual::write_real(R64 real, R64 precision) -> void {
  if (!valid_state) {
    return;
  }

  constexpr R64 signed_limit = 9'223'372'036'854'775'808.0;
  if (!__builtin_isfinite(real) || real >= signed_limit ||
      real < -signed_limit) {
    valid_state = False;
    return;
  }

  constexpr auto max_length = 32;
  S64 decimal_portion = S64(real);
  auto length = decimal_length<S64, U64>(decimal_portion);
  valid_state &=
      write_decimal<S64, U64>(source, cursor, decimal_portion, length);

  valid_state &= cursor < source.get_size();
  if (!valid_state) {
    return;
  }

  // Keep the radix point for integral inputs so real output remains distinct
  // from integer output.
  auto data = source.get_data();
  data[cursor++] = '.';
  length += 1;

  auto fract = real - decimal_portion;

  // Convert a negative fractional remainder to its magnitude. An integral value
  // still receives one zero after the radix point.
  if (fract <= 0) {
    fract *= -1;
    if (fract < precision) {
      valid_state &= cursor < source.get_size();
      if (!valid_state) {
        return;
      }

      data[cursor++] = '0';
      length += 1;
    }
  }

  // Emit successive base 10 digits until the remaining fraction falls below the
  // source precision or the bounded representation reaches `max_length`.
  while (length < max_length && fract > precision) {
    if (cursor >= source.get_size()) {
      valid_state &= cursor < source.get_size();
      return;
    }

    fract *= 10;
    precision *= 10;
    length += 1;

    // Binary scaling can round a value below ten up to ten. Clamp that
    // representation rather than emit a nondecimal byte.
    U8 value;
    if (fract >= 10) {
      value = 9;
      fract = 0;
    } else {
      value = U8(fract);
      fract -= value;
      // A remainder just below one belongs to the current digit when increment
      // does not require carrying through earlier output. A nine keeps that
      // remainder for the next digit instead.
      if (fract > 0.99999 && value < 9) {
        value++;
        fract = 0;
      }
    }

    data[cursor++] = '0' + value;
  }
}

auto Writer::Textual::operator<<(const View::Bytes raw) -> Writer::Textual& {
  valid_state = valid_state && write_text(source, cursor, raw);
  return *this;
}
