// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/memory/lifetime.hpp"

#include "perimortem/core/data.hpp"
#include "perimortem/core/option.hpp"

namespace Perimortem::Core::Static {

// Union holds either no value or one value from a fixed set of distinct,
// trivially destructible types. The selected value lives directly in Union
// storage, so selection requires no allocation. The U8 tag reserves zero for
// the empty representation and limits the set to 254 alternatives.
//
// Construction accepts only an exact alternative type and preserves whether
// its source is an lvalue or rvalue for that alternative's constructor. Copy
// operations exist only when every alternative can be copied. Move operations
// exist only when every alternative accepts an rvalue. The source remains
// selected after moving a Union.
//
// `find` returns an optional reference to one requested alternative. `visit`
// calls the matching visitor or the zero argument visitor for the empty
// representation.
template <typename... value_types>
class Union {
 private:
  static_assert(
      sizeof...(value_types) != 0,
      "Union requires at least one provided type.");
  static_assert(
      sizeof...(value_types) < 255,
      "Union supports at most 254 types plus null.");

  template <typename value_type>
  static consteval auto type_count() -> Count {
    return (Count(__is_same(value_type, value_types)) + ...);
  }

  static_assert(
      ((type_count<value_types>() == 1) && ...),
      "All provided Union types must be unique.");
  static_assert(
      (__is_same(value_types, __remove_cvref(value_types)) && ...),
      "Union alternatives must be unqualified value types.");
  static_assert(
      (__is_trivially_destructible(value_types) && ...),
      "All provided Union types must be trivially destructible.");

  static consteval auto storage_size() -> Count {
    Count size = 0;
    ((size = size < sizeof(value_types) ? sizeof(value_types) : size), ...);
    return size;
  }

  static consteval auto storage_alignment() -> Count {
    Count alignment = 0;
    ((alignment =
          alignment < alignof(value_types) ? alignof(value_types) : alignment),
     ...);
    return alignment;
  }

  template <typename value_type>
  static consteval auto type_tag() -> U8 {
    U8 result = 0;
    U8 candidate = 1;
    ((result = __is_same(value_type, value_types) ? candidate : result,
      candidate++),
     ...);
    return result;
  }

  template <typename Candidate>
  using Alternative = __remove_cvref(Candidate);

  template <typename Candidate>
  static consteval auto accepts() -> bool {
    if constexpr (type_count<Alternative<Candidate>>() != 1) {
      return false;
    } else {
      return __is_constructible(Alternative<Candidate>, Candidate&&);
    }
  }

  static constexpr bool copy_constructible =
      (__is_constructible(value_types, const value_types&) && ...);
  static constexpr bool move_constructible =
      (__is_constructible(value_types, value_types&&) && ...);

  template <typename value_type, typename Candidate>
  constexpr auto construct(Candidate& candidate) -> value_type& {
    value_type& value = Toolchain::Memory::Lifetime::construct<value_type>(
        storage, [&candidate] {
          if constexpr (__is_lvalue_reference(Candidate)) {
            return value_type(candidate);
          } else {
            return value_type(Data::take(candidate));
          }
        });

    tag = type_tag<value_type>();
    return value;
  }

  template <typename value_type>
  constexpr auto active() -> value_type& {
    return *Data::cast<value_type>(storage);
  }

  template <typename value_type>
  constexpr auto active() const -> const value_type& {
    return *Data::cast<const value_type>(storage);
  }

  template <typename value_type, typename... Rest, typename Source>
  constexpr auto construct_active(Source& source) -> void {
    if (source.tag == type_tag<value_type>()) {
      if constexpr (__is_same(Source, const Union)) {
        construct<value_type, const value_type&>(
            source.template active<value_type>());
      } else {
        construct<value_type, value_type>(source.template active<value_type>());
      }

      return;
    }

    if constexpr (sizeof...(Rest) != 0) {
      construct_active<Rest...>(source);
    }
  }

  template <
      typename value_type,
      typename... Rest,
      typename Self,
      typename Visitor>
  constexpr static auto visit_active(Self& self, Visitor& visitor)
      -> decltype(auto) {
    if (self.tag == type_tag<value_type>()) {
      return visitor(self.template active<value_type>());
    }

    if constexpr (sizeof...(Rest) != 0) {
      return visit_active<Rest...>(self, visitor);
    }

    __builtin_unreachable();
  }

  template <typename Self, typename... Cases>
  constexpr static auto dispatch(Self& self, Cases... cases) -> decltype(auto) {
    struct Visitor : Cases... {
      using Cases::operator()...;
    } visitor{Data::take(cases)...};

    if (self.is_null()) {
      return visitor();
    }

    return visit_active<value_types...>(self, visitor);
  }

  alignas(storage_alignment()) U8 storage[storage_size()];
  U8 tag = 0;

 public:
  constexpr Union() = default;

  // Alternative construction is explicit so a value cannot silently select a
  // Union alternative at an unrelated call boundary.
  template <typename Candidate>
    requires(accepts<Candidate>())
  explicit constexpr Union(Candidate&& value) {
    construct<Alternative<Candidate>, Candidate>(value);
  }

  constexpr Union(const Union& source)
    requires(copy_constructible)
  {
    if (!source.is_null()) {
      construct_active<value_types...>(source);
    }
  }

  constexpr Union(const Union&)
    requires(!copy_constructible)
  = delete;

  constexpr Union(Union&& source)
    requires(move_constructible)
  {
    if (!source.is_null()) {
      construct_active<value_types...>(source);
    }
  }

  constexpr Union(Union&&)
    requires(!move_constructible)
  = delete;

  constexpr auto operator=(const Union& source) -> Union&
    requires(copy_constructible)
  {
    if (this == &source) {
      return *this;
    }

    tag = 0;
    if (!source.is_null()) {
      construct_active<value_types...>(source);
    }

    return *this;
  }

  constexpr auto operator=(const Union&) -> Union&
    requires(!copy_constructible)
  = delete;

  constexpr auto operator=(Union&& source) -> Union&
    requires(move_constructible)
  {
    if (this == &source) {
      return *this;
    }

    tag = 0;
    if (!source.is_null()) {
      construct_active<value_types...>(source);
    }

    return *this;
  }

  constexpr auto operator=(Union&&) -> Union&
    requires(!move_constructible)
  = delete;

  template <typename value_type>
  constexpr auto is() const -> Bool {
    static_assert(
        type_count<value_type>() == 1,
        "Requested value type is not a Union alternative.");
    return tag == type_tag<value_type>();
  }

  template <typename value_type>
  constexpr auto find() -> Core::Option<value_type&> {
    static_assert(
        type_count<value_type>() == 1,
        "Requested value type is not a Union alternative.");
    if (!is<value_type>()) {
      return {};
    }

    return active<value_type>();
  }

  template <typename value_type>
  constexpr auto find() const -> Core::Option<const value_type&> {
    static_assert(
        type_count<value_type>() == 1,
        "Requested value type is not a Union alternative.");
    if (!is<value_type>()) {
      return {};
    }

    return active<value_type>();
  }

  template <typename... Cases>
  constexpr auto visit(Cases... cases) -> decltype(auto) {
    return dispatch(*this, Data::take(cases)...);
  }

  template <typename... Cases>
  constexpr auto visit(Cases... cases) const -> decltype(auto) {
    return dispatch(*this, Data::take(cases)...);
  }

  constexpr auto is_null() const -> Bool { return tag == 0; }
};

}  // namespace Perimortem::Core::Static
