// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/algorithm/search.hpp"

#include "toolchain/validation/test.hpp"

#include "perimortem/core/null_terminated.hpp"
#include "perimortem/core/static/vector.hpp"

using namespace Perimortem::Core;
using namespace Toolchain::Validation;

static Harness AlgoSearch = {
  .name = "Core::Algorithm::Search",
};

template <Count size>
auto place_text(Static::Bytes<size>& source, View::Bytes text, Count location)
    -> void {
  Data::copy(source.get_data() + location, text.get_data(), text.get_size());
}

template <Count size>
auto make_search_source(View::Bytes text, Count location)
    -> Static::Bytes<size> {
  Static::Bytes<size> source;
  for (Count index = 0; index < source.get_size(); index++) {
    source[index] = U8('x');
  }

  place_text(source, text, location);
  return source;
}

VALIDATION_TEST(AlgoSearch, boundaries) {
  EXPECT_EQ(Algorithm::Search::match(""_view, ""_view), Count(0));
  EXPECT_EQ(Algorithm::Search::match("abc"_view, ""_view), Count(0));
  EXPECT_EQ(Algorithm::Search::match(""_view, "abc"_view), Count(-1));
  EXPECT_EQ(Algorithm::Search::match("a"_view, "a"_view), Count(0));
  EXPECT_EQ(Algorithm::Search::match("a"_view, "b"_view), Count(-1));
}

VALIDATION_TEST(AlgoSearch, byte_offset) {
  Static::Bytes<160> source;
  for (Count i = 0; i < source.get_size(); i++) {
    source[i] = 'a';
  }

  source[97] = '\\';

  EXPECT_EQ(Algorithm::Search::match(source, U8('\\')), Count(97));
  EXPECT_EQ(Algorithm::Search::match(source, U8('z')), Count(-1));
}

VALIDATION_TEST(AlgoSearch, extrema) {
  const U32 values[] = {4, 2, 7, 2, 7};
  const View::Vector<U32> source(values);
  EXPECT_EQ(Algorithm::Search::min(source), Count(1));
  EXPECT_EQ(Algorithm::Search::max(source), Count(2));
}

VALIDATION_TEST(AlgoSearch, match) {
  constexpr auto test_word = "world"_view;
  EXPECT_EQ(
      Algorithm::Search::match(make_search_source<5>(test_word, 0), test_word),
      Count(0));
  EXPECT_EQ(
      Algorithm::Search::match(make_search_source<70>(test_word, 0), test_word),
      Count(0));
  EXPECT_EQ(
      Algorithm::Search::match(
          make_search_source<70>(test_word, 40), test_word),
      Count(40));
  EXPECT_EQ(
      Algorithm::Search::match(
          make_search_source<70>(test_word, 65), test_word),
      Count(65));
}

VALIDATION_TEST(AlgoSearch, vectorized_match) {
  constexpr auto test_word = "world"_view;
  constexpr auto near_match = "woold"_view;
  constexpr Count avx2_width = 32;
  constexpr Count match_location = avx2_width * 30 - 2;
  constexpr Count near_match_locations[] = {
    avx2_width - 2,  avx2_width * 5,      avx2_width * 11 - 2,
    avx2_width * 17, avx2_width * 23 - 2,
  };

  auto source = make_search_source<1240>(test_word, match_location);
  for (Count location : near_match_locations) {
    place_text(source, near_match, location);
  }

  // False candidates share the first and last bytes used by the AVX2 filter.
  // The valid match crosses a 32 byte boundary, forcing its verification load
  // to span two vector regions.
  EXPECT_EQ(Algorithm::Search::match(source, test_word), match_location);

  place_text(source, near_match, match_location);
  EXPECT_EQ(Algorithm::Search::match(source, test_word), Count(-1));
}

VALIDATION_TEST(AlgoSearch, multiple_matches) {
  constexpr auto test_word = "perimortem testing"_view;
  constexpr Static::Vector<Count, 12> word_locations = {{
    12,
    120,
    245,
    381,
    422,
    589,
    690,
    690 + test_word.get_size(),
    690 + test_word.get_size() * 2,
    690 + test_word.get_size() * 3,
    810,
    830,
  }};

  Static::Bytes<
      word_locations[word_locations.get_size() - 1] + test_word.get_size() + 1>
      test_text;
  for (Count i = 0; i < word_locations.get_size(); i++) {
    place_text(test_text, test_word, word_locations[i]);
  }

  for (Count i = 0; i < word_locations.get_size(); i++) {
    EXPECT_EQ(
        Algorithm::Search::match(test_text, test_word), word_locations[i]);
    place_text(test_text, "deleted"_view, word_locations[i]);
  }
}

VALIDATION_TEST(AlgoSearch, very_large_key) {
  constexpr auto test_word = "perimortem testing on very large strings"_view;
  constexpr Static::Vector<Count, 11> word_locations = {{
    12,
    120,
    245,
    381,
    422,
    589,
    690,
    690 + test_word.get_size(),
    690 + test_word.get_size() * 2,
    690 + test_word.get_size() * 3,
    930,
  }};

  Static::Bytes<
      word_locations[word_locations.get_size() - 1] + test_word.get_size() + 1>
      test_text;
  for (Count i = 0; i < word_locations.get_size(); i++) {
    place_text(test_text, test_word, word_locations[i]);
  }

  for (Count i = 0; i < word_locations.get_size(); i++) {
    EXPECT_EQ(
        Algorithm::Search::match(test_text, test_word), word_locations[i]);
    place_text(test_text, "deleted"_view, word_locations[i]);
  }
}
