// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.hpp"

#include "perimortem/core/data.hpp"

namespace Perimortem::Core::View {

// Selection iterates over values selected from a View by one predicate. It
// stores the source View, predicate and current position. The same type serves
// as the range and its begin and end iterators.
template <
    typename view_type,
    typename predicate_type = Bool (*)(const typename view_type::data_type&)>
class Selection {
 public:
  using data_type = typename view_type::data_type;

  constexpr Selection(view_type source) : Selection(source, accept, 0, True) {}

  constexpr Selection(view_type source, predicate_type predicate)
      : Selection(source, Data::take(predicate), 0, True) {}

  constexpr Selection(view_type source, Count position)
      : Selection(source, accept, position, False) {}

  constexpr auto operator*() const -> data_type {
    return source.get_data()[position];
  }

  constexpr auto operator++() -> Selection& {
    position++;
    select();
    return *this;
  }

  constexpr auto operator!=(const Selection& rhs) const -> Bool {
    return position != rhs.position;
  }

  constexpr auto operator==(const Selection& rhs) const -> Bool {
    return position == rhs.position;
  }

  constexpr auto begin() const -> Selection {
    return Selection(source, predicate, 0, True);
  }

  constexpr auto end() const -> Selection {
    return Selection(source, predicate, source.get_size(), False);
  }

 private:
  static constexpr auto accept(const data_type&) -> Bool { return True; }

  constexpr Selection(
      view_type source,
      predicate_type predicate,
      Count position,
      Bool select_position)
      : source(source), predicate(Data::take(predicate)), position(position) {
    if (select_position) {
      select();
    }
  }

  constexpr auto select() -> void {
    while (position < source.get_size() &&
           !predicate(source.get_data()[position])) {
      position++;
    }
  }

  view_type source;
  predicate_type predicate;
  Count position;
};

template <typename view_type>
Selection(view_type) -> Selection<view_type>;

template <typename view_type, typename predicate_type>
Selection(view_type, predicate_type) -> Selection<view_type, predicate_type>;

}  // namespace Perimortem::Core::View
