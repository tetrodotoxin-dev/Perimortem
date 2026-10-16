// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/memory/lifetime.hpp"
#include "toolchain/toolchain.hpp"

#include "perimortem/core/data.hpp"

namespace Perimortem::Core {

// Option stores either one value directly or None. A function can therefore
// return a value created on its stack without allocating separate storage.
// Construction and destruction follow the selected value's lifetime, including
// for types that can only move or lack default construction.
//
// A boolean check establishes the selected state before pointer shaped access.
// Visit handles both outcomes through separate callbacks that return one common
// value.
template <typename value_type>
class Option {
 private:
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

  constexpr Option(const Option& source)
    requires(__is_constructible(value_type, const value_type&))
      : set(false) {
    if (source.set) {
      construct(source.value);
    }
  }

  constexpr Option(Option&& source)
    requires(__is_constructible(value_type, value_type &&))
      : set(false) {
    if (source.set) {
      construct(Data::take(source.value));
    }
  }

  constexpr ~Option() { clear(); }

  constexpr auto operator=(const Option& source) -> Option&
    requires(__is_constructible(value_type, const value_type&))
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

  constexpr auto operator=(Option&& source) -> Option&
    requires(__is_constructible(value_type, value_type &&))
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

  constexpr explicit operator bool() const { return bool(set); }
  constexpr explicit operator Bool() const { return set; }

  constexpr auto operator*() -> value_type& { return value; }
  constexpr auto operator*() const -> const value_type& { return value; }
  constexpr auto operator->() -> value_type* { return &value; }
  constexpr auto operator->() const -> const value_type* { return &value; }

  template <typename RejectCallback, typename ValueVisitor>
  constexpr auto visit(
      RejectCallback reject_callback,
      ValueVisitor value_visitor) -> decltype(auto) {
    if (!set) {
      return reject_callback();
    }

    return value_visitor(value);
  }

  template <typename RejectCallback, typename ValueVisitor>
  constexpr auto visit(
      RejectCallback reject_callback,
      ValueVisitor value_visitor) const -> decltype(auto) {
    if (!set) {
      return reject_callback();
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

  // Requiring an lvalue ties the stored address to an object selected by the
  // caller with a lifetime that can cover every use of the Option.
  Option(value_type&&) = delete;

  constexpr explicit operator bool() const { return value != nullptr; }
  constexpr explicit operator Bool() const { return value != nullptr; }

  constexpr auto operator*() const -> value_type& { return *value; }
  constexpr auto operator->() const -> value_type* { return value; }

  template <typename RejectCallback, typename ReferenceVisitor>
  constexpr auto visit(
      RejectCallback reject_callback,
      ReferenceVisitor reference_visitor) const -> decltype(auto) {
    if (value == nullptr) {
      return reject_callback();
    }

    return reference_visitor(*value);
  }

 private:
  value_type* value = nullptr;
};

}  // namespace Perimortem::Core
