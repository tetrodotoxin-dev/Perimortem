// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/memory/lifetime.hpp"
#include "toolchain/toolchain.hpp"

#include "perimortem/core/data.hpp"

namespace Perimortem::Core {

// Option stores either one value directly or None without allocating separate
// storage. The value does not need to support default construction. Copy and
// move operations exist only when the value supports the corresponding
// construction. Moving keeps the source selected. Its value has the state
// produced by `value_type`'s move constructor.
//
// Presence checks observe the state without exposing the value. `visit` is the
// only extraction boundary and requires callbacks for both the selected value
// and None. Both callbacks return one common type.
template <typename value_type>
class Option {
 private:
  static constexpr bool trivial_value = __is_trivially_copyable(value_type);
  static constexpr bool copy_constructible =
      __is_constructible(value_type, const value_type&);
  static constexpr bool move_constructible =
      __is_constructible(value_type, value_type&&);

  constexpr auto construct(const value_type& candidate) -> void {
    Toolchain::Memory::Lifetime::construct<value_type>(
        &value, [&candidate] { return value_type(candidate); });

    set = true;
  }

  constexpr auto construct(value_type&& candidate) -> void {
    Toolchain::Memory::Lifetime::construct<value_type>(
        &value, [&candidate] { return value_type(Data::take(candidate)); });

    set = true;
  }

  constexpr auto clear() -> void {
    if (set) {
      value.~value_type();
      set = false;
    }
  }

  union {
    value_type value;
  };

  Bool set = false;

 public:
  constexpr Option() : set(false) {}

  constexpr Option(const value_type& selected)
    requires(__is_constructible(value_type, const value_type&))
      : value(selected), set(true) {}

  constexpr Option(value_type&& selected)
    requires(__is_constructible(value_type, value_type &&))
      : value(Data::take(selected)), set(true) {}

  constexpr Option(const Option&)
    requires(trivial_value)
  = default;

  constexpr Option(const Option& source)
    requires(!trivial_value && copy_constructible)
      : set(false) {
    if (source.set) {
      construct(source.value);
    }
  }

  constexpr Option(const Option&)
    requires(!trivial_value && !copy_constructible)
  = delete;

  constexpr Option(Option&&)
    requires(trivial_value)
  = default;

  constexpr Option(Option&& source)
    requires(!trivial_value && move_constructible)
      : set(false) {
    if (source.set) {
      construct(Data::take(source.value));
    }
  }

  constexpr Option(Option&&)
    requires(!trivial_value && !move_constructible)
  = delete;

  constexpr ~Option()
    requires(trivial_value)
  = default;

  constexpr ~Option()
    requires(!trivial_value)
  {
    clear();
  }

  constexpr auto operator=(const Option&) -> Option&
    requires(trivial_value)
  = default;

  constexpr auto operator=(const Option& source) -> Option&
    requires(!trivial_value && copy_constructible)
  {
    if (this == &source) {
      return *this;
    }

    clear();
    if (source.set) {
      construct(source.value);
    }

    return *this;
  }

  constexpr auto operator=(const Option&) -> Option&
    requires(!trivial_value && !copy_constructible)
  = delete;

  constexpr auto operator=(Option&&) -> Option&
    requires(trivial_value)
  = default;

  constexpr auto operator=(Option&& source) -> Option&
    requires(!trivial_value && move_constructible)
  {
    if (this == &source) {
      return *this;
    }

    clear();
    if (source.set) {
      construct(Data::take(source.value));
    }

    return *this;
  }

  constexpr auto operator=(Option&&) -> Option&
    requires(!trivial_value && !move_constructible)
  = delete;

  constexpr explicit operator bool() const { return bool(set); }
  constexpr explicit operator Bool() const { return set; }

  template <typename ValueVisitor, typename NoneVisitor>
  constexpr auto visit(ValueVisitor value_visitor, NoneVisitor none_visitor)
      -> decltype(auto) {
    if (!set) {
      return none_visitor();
    }

    return value_visitor(value);
  }

  template <typename ValueVisitor, typename NoneVisitor>
  constexpr auto visit(ValueVisitor value_visitor, NoneVisitor none_visitor)
      const -> decltype(auto) {
    if (!set) {
      return none_visitor();
    }

    return value_visitor(value);
  }
};

// A reference Option stores the address of its selected object and remains
// pointer sized. A const Option does not change the referent's type because
// constness belongs to the reference declared at the API boundary. The caller
// keeps the selected object alive while accessing it through the Option.
template <typename value_type>
class Option<value_type&> {
 public:
  constexpr Option() = default;
  constexpr Option(value_type& value) : value(&value) {}

  // Rejecting temporaries prevents an address that is guaranteed to expire at
  // the end of the construction expression. The caller still keeps an accepted
  // lvalue alive through every use of the Option.
  Option(value_type&&) = delete;

  constexpr explicit operator bool() const { return value != nullptr; }
  constexpr explicit operator Bool() const { return value != nullptr; }

  template <typename ReferenceVisitor, typename NoneVisitor>
  constexpr auto visit(
      ReferenceVisitor reference_visitor,
      NoneVisitor none_visitor) const -> decltype(auto) {
    if (value == nullptr) {
      return none_visitor();
    }

    return reference_visitor(*value);
  }

 private:
  value_type* value = nullptr;
};

}  // namespace Perimortem::Core
