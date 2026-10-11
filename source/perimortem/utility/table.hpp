// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/data.hpp"
#include "perimortem/core/option.hpp"
#include "perimortem/core/static/vector.hpp"
#include "perimortem/core/view/bytes.h"
#include "perimortem/core/view/vector.hpp"
#include "perimortem/utility/pair.hpp"

namespace Perimortem::Utility {

// Table compiles a fixed set of byte keys into one immutable lookup layout. Key
// length selects a bucket directly, and each bucket packs its key bytes inline
// beside mapping indexes without per entry pointers or hashes.
//
// `source` has static storage because the referenced object participates in the
// Table type. Different source arrays therefore produce distinct tables. This
// makes Table suitable for small vocabularies known at compile time rather than
// mutable data.
//
// Keyword benchmarks with low hit rates measured more than twice the throughput
// of `Dynamic::Map`. `cache_line_size` can keep a complete length bucket within
// one cache line when its packed bytes fit.
//
// The compact coordinates admit from 1 through 255 entries and 65535 bytes of
// packed storage. Every key contains at least one byte. Bucket alignment counts
// toward storage. Sources outside those limits fail during compilation.
template <typename value_type, const auto& source, U64 cache_line_size = 1>
class Table {
 private:
  // Accepts both a raw C array and a View::Vector as source.
  static consteval auto get_source_count() -> Count {
    if constexpr (requires { source.get_size(); }) {
      return source.get_size();
    } else {
      return Core::Data::array_size(source);
    }
  }

  static constexpr Count source_count = get_source_count();

  static consteval auto has_empty_key() -> Bool {
    for (Count i = 0; i < source_count; i++) {
      if (source[i].key.is_empty()) {
        return True;
      }
    }

    return False;
  }

  static_assert(cache_line_size > 0, "Table cache line size cannot be zero.");
  static_assert(source_count > 0, "Table requires at least one entry.");
  static_assert(
      source_count <= U8(-1),
      "Table U8 coordinates support at most 255 entries.");
  static_assert(!has_empty_key(), "Table does not support empty keys.");

  static consteval auto required_storage() -> Count {
    Count buckets[max_length()] = {0};
    for (Count i = 0; i < get_source_count(); i++) {
      buckets[source[i].key.get_size()] += source[i].key.get_size();
    }

    Count total = 0;
    for (Count i = 0; i < max_length(); i++) {
      const auto cache_alignment = (total % cache_line_size);
      if (cache_alignment != 0 &&
          cache_alignment + buckets[i] > cache_line_size) {
        total += cache_line_size - cache_alignment;
      }

      total += buckets[i];
    }

    return total;
  }

  // Key length indexes its bucket directly. Reserve an empty sentinel one byte
  // beyond the longest key so that immediately oversized lookups return no
  // items without accessing key storage.
  static consteval auto max_length() -> Count {
    Count max = 0;
    for (Count i = 0; i < get_source_count(); i++) {
      max = max > source[i].key.get_size() + 1 ? max
                                               : source[i].key.get_size() + 1;
    }

    return max;
  }

  static constexpr Count storage_size = required_storage();
  static constexpr Count max_range = max_length();

  static_assert(
      storage_size <= U16(-1),
      "Table U16 coordinates support at most 65535 bytes of packed storage.");

  struct Storage {
    consteval Storage() {
      // Caculate the bytes required for each bucket
      Count buckets[max_length()] = {0};
      for (Count i = 0; i < get_source_count(); i++) {
        buckets[source[i].key.get_size()] += source[i].key.get_size();
      }

      // Align and set all bucket indexes
      Count total = 0;
      for (Count i = 0; i < max_length(); i++) {
        // Advance a bucket that would straddle `cache_line_size` to the next
        // boundary. A bucket no larger than that boundary can then be scanned
        // from one cache line.
        const auto cache_alignment = (total % cache_line_size);
        if (cache_alignment != 0 &&
            cache_alignment + buckets[i] > cache_line_size) {
          total += cache_line_size - cache_alignment;
        }

        buffer_coordinates[i].byte_index = total;
        total += buckets[i];

        buckets[i] = buffer_coordinates[i].byte_index;
      }

      total = 0;
      for (Count size = 0; size < max_length(); size++) {
        buffer_coordinates[size].item_index = total;
        for (Count i = 0; i < get_source_count(); i++) {
          if (source[i].key.get_size() != size) {
            continue;
          }

          mappings[total] = source[i].value;
          buffer_coordinates[size].item_count++;
          total++;
          for (Count c = 0; c < source[i].key.get_size(); c++) {
            buffer[buckets[size]++] = source[i].key[c];
          }
        }
      }
    }

    struct Coord {
      U8 item_index = 0;
      U8 item_count = 0;
      U16 byte_index = 0;
    };

    alignas(64) Core::Static::Vector<U8, storage_size> buffer = {};
    Core::Static::Vector<Coord, max_range + 1> buffer_coordinates = {};
    Core::Static::Vector<value_type, source_count> mappings = {};
  };

  static constexpr Storage byte_pack;

  static constexpr auto find_or_null(const Core::View::Bytes key)
      -> const value_type* {
    const auto range_step = key.get_size();
    if (range_step > max_range) {
      return nullptr;
    }

    const auto entry = byte_pack.buffer_coordinates.get_data()[range_step];
    const auto item_start = entry.item_index;
    const auto item_count = entry.item_count;
    const auto byte_start = entry.byte_index;
    for (U8 i = 0; i < item_count; i++) {
      const auto byte_index = byte_start + i * range_step;
      if (byte_pack.buffer[byte_index] != key[0]) {
        continue;
      }

      if (Core::Data::compare(
              byte_pack.buffer.get_data() + byte_index, key.get_data(),
              range_step)) {
        return &byte_pack.mappings[item_start + i];
      }
    }

    return nullptr;
  }

 public:
  static constexpr auto find(const Core::View::Bytes key)
      -> Core::Option<const value_type&> {
    auto value = find_or_null(key);
    return value == nullptr ? Core::Option<const value_type&>() : *value;
  }

  static consteval auto get_values() -> Core::View::Vector<value_type> {
    return byte_pack.mappings;
  }
};

}  // namespace Perimortem::Utility
