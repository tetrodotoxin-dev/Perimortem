// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.hpp"

#include "perimortem/core/access/bytes.h"
#include "perimortem/core/storage/option.hpp"
#include "perimortem/core/view/bytes.h"
#include "perimortem/core/view/vector.hpp"

namespace Perimortem::Serialization {

// Version represents an ordered sequence of exactly `component_extent` U16
// components. The component count is part of the type and defaults to two.
// Construction and parsing zero fill unspecified trailing components, while
// comparison reads every component from left to right. Every sequence,
// including all zeroes, is a valid version. Absence is represented by Option
// instead of reserving one sequence.
//
// Text contains decimal components separated by periods. Leading zeroes and
// values larger than U16 are rejected so every accepted component has one
// representation. Parsing stops before a suffix that cannot begin another
// decimal component. A period followed by a decimal digit always begins a new
// component and returns None when accepting that component would narrow the
// source into the selected component count.
template <Count component_extent = 2>
  requires(component_extent > 0)
class Version {
 private:
  struct Components {
    U16 source_block[component_extent];
  };

 public:
  constexpr Version() = default;

  // Aggregate construction accepts up to `component_extent` components and
  // zero fills the remainder. Call sites use an extra pair of braces, as in
  // `Version<4> version = {{1, 2}}`.
  constexpr Version(const Components& components) : components(components) {}

  static constexpr auto parse(Core::View::Bytes text)
      -> Core::Storage::Option<Version> {
    if (text.is_empty() || text[0] < '0' || text[0] > '9') {
      return {};
    }

    Version parsed;
    Count component = 0;
    Count digits = 0;

    for (Count i = 0; i < text.get_size(); i++) {
      const U8 byte = text[i];
      if (byte >= '0' && byte <= '9') {
        const U16 digit = U16(byte - '0');
        if (digits != 0 && parsed.components.source_block[component] == 0) {
          return {};
        }

        if (parsed.components.source_block[component] >
            (U16(-1) - digit) / 10) {
          return {};
        }

        parsed.components.source_block[component] =
            U16(parsed.components.source_block[component] * 10 + digit);
        digits++;
        continue;
      }

      if (byte != '.' || i + 1 == text.get_size() || text[i + 1] < '0' ||
          text[i + 1] > '9') {
        break;
      }

      if (component + 1 == component_extent) {
        return {};
      }

      component++;
      digits = 0;
    }

    return parsed;
  }

  constexpr auto operator==(const Version& rhs) const -> Bool {
    for (Count i = 0; i < component_extent; i++) {
      if (components.source_block[i] != rhs.components.source_block[i]) {
        return False;
      }
    }

    return True;
  }

  constexpr auto operator!=(const Version& rhs) const -> Bool {
    return !(*this == rhs);
  }

  constexpr auto operator<(const Version& rhs) const -> Bool {
    for (Count i = 0; i < component_extent; i++) {
      if (components.source_block[i] != rhs.components.source_block[i]) {
        return components.source_block[i] < rhs.components.source_block[i];
      }
    }

    return False;
  }

  constexpr auto operator>(const Version& rhs) const -> Bool {
    return rhs < *this;
  }

  // Serializes every component into caller supplied storage and returns a View
  // borrowing the written range. Trailing zeroes remain in the text, so the
  // selected component count determines the exact output shape. Insufficient
  // storage returns None without changing the supplied bytes.
  constexpr auto serialize(Core::Access::Bytes output) const
      -> Core::Storage::Option<Core::View::Bytes> {
    Count required_size = component_extent - 1;
    for (Count i = 0; i < component_extent; i++) {
      required_size += decimal_size(components.source_block[i]);
    }

    if (output.get_size() < required_size) {
      return {};
    }

    Count cursor = 0;
    for (Count i = 0; i < component_extent; i++) {
      if (i != 0) {
        output.get_data()[cursor++] = '.';
      }

      U16 value = components.source_block[i];
      const Count end = cursor + decimal_size(value);
      Count write = end;
      do {
        output.get_data()[--write] = U8('0' + value % 10);
        value /= 10;
      } while (write != cursor);

      cursor = end;
    }

    return Core::View::Bytes(output.get_data(), required_size);
  }

  static constexpr auto get_component_count() -> Count {
    return component_extent;
  }

  static constexpr auto get_max_serialized_size() -> Count {
    return component_extent * 6 - 1;
  }

  // Returns every component in comparison order. The View remains valid while
  // this Version remains alive at the same address.
  constexpr auto get_components() const -> Core::View::Vector<U16> {
    return Core::View::Vector<U16>(components.source_block, component_extent);
  }

 private:
  static constexpr auto decimal_size(U16 value) -> Count {
    Count size = 1;
    while (value >= 10) {
      value /= 10;
      size++;
    }

    return size;
  }

  Components components{};
};

}  // namespace Perimortem::Serialization
