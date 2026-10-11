// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.hpp"

// The target C runtime owns these declarations, including its import and
// exception specifications. Clang recognizes the same memory intrinsics
// without redeclaring a different C boundary in each host configuration.
#include <string.h>

namespace Perimortem::Core {

class Data {
 public:
  enum class ByteOrder {
    Little = __ORDER_LITTLE_ENDIAN__,
    Big = __ORDER_BIG_ENDIAN__,
    Native = __BYTE_ORDER__,
  };

  // Type helpers
  template <typename storage>
  static consteval auto size_in_bits() -> U64 {
    return sizeof(storage) * 8;
  }

  template <typename type, Count size>
  static consteval auto array_size(const type (&)[size]) -> Count {
    return size;
  }

  // Aligns data by moving to the next valid offset of alignment_size.
  template <Count alignment_size>
  static constexpr auto align(Count offset) -> Count {
    if constexpr (alignment_size <= 1) {
      return offset;
    }

    constexpr Count alignment_filter = alignment_size - 1;
    // Check if alignment_size is a power of two, if it is we can avoid a
    // modulo. Any easy test for only a single set bit is to subtract one from
    // the value and and it with itself. It will return nonzero for any non
    // power of two.
    //
    // This technically let's zero through but we special case zero and one.
    if constexpr ((alignment_size & alignment_filter) == 0) {
      const auto required_alignment = (~offset + 1) & (alignment_filter);
      return offset + required_alignment;
    } else {
      const auto required_alignment =
          alignment_size - (offset % alignment_size);
      return required_alignment == alignment_size ? offset
                                                  : offset + required_alignment;
    }
  }

  // Used for reading const raw data.
  template <typename target_type>
  static auto cast(const void* source) -> const target_type* {
    // TODO: start_lifetime_as
    return reinterpret_cast<const target_type*>(source);
  }

  // Used for reading raw data.
  template <typename target_type>
  static auto cast(void* source) -> target_type* {
    // TODO: start_lifetime_as
    return reinterpret_cast<target_type*>(source);
  }

  // Copies one complete object representation into caller supplied storage.
  template <typename storage_type>
  static auto copy(U8* dest, const storage_type* src) -> storage_type* {
    return reinterpret_cast<storage_type*>(
        memcpy(dest, src, U64(sizeof(storage_type))));
  }

  // Copies `count` consecutive object representations. A zero count performs
  // no memory access and returns `dest` unchanged.
  template <typename storage_type>
  static auto copy(U8* dest, const storage_type* src, Count count)
      -> storage_type* {
    if (count == 0) {
      return reinterpret_cast<storage_type*>(dest);
    }

    return reinterpret_cast<storage_type*>(
        memcpy(dest, src, U64(sizeof(storage_type)) * count));
  }

  // Copies one value's object representation into caller supplied storage.
  template <typename storage_type>
    requires(!__is_pointer(storage_type))
  static auto copy(U8* dest, storage_type src) -> storage_type* {
    return reinterpret_cast<storage_type*>(
        memcpy(dest, &src, U64(sizeof(storage_type))));
  }

  static auto set(U8* dest, U8 value, Count count = 1) -> void {
    memset(dest, value, count);
  }

  template <typename storage_type>
  static auto take(storage_type& source) -> storage_type&& {
    return static_cast<storage_type&&>(source);
  }

  template <typename storage_type>
  static constexpr auto compare(
      const storage_type* dest,
      const storage_type* src,
      Count count = 1) -> Bool {
    if consteval {
      for (Count i = 0; i < count; i++) {
        if (dest[i] != src[i]) {
          return false;
        }
      }

      return true;
    } else {
      return memcmp(dest, src, U64(sizeof(storage_type)) * count) == 0;
    }
  }

  template <ByteOrder source, ByteOrder target, typename storage_type>
  static constexpr auto ensure_endian(storage_type value) -> storage_type {
    if constexpr (source != target) {
      switch (sizeof(storage_type)) {
      case 1:
        return value;
      case 2:
        return __builtin_bswap16(value);
      case 4:
        return __builtin_bswap32(value);
      case 8:
        return __builtin_bswap64(value);
      }
    }

    return value;
  }

  // Reads a fixed width integer from packed bytes in a declared byte order. The
  // runtime copy accepts every source alignment, while constant evaluation
  // assembles the value through its unsigned representation. Signed results
  // retain the exact two's complement bits authored by the stream.
  template <ByteOrder source, typename storage_type>
  static constexpr auto read(const U8* source_data) -> storage_type {
    static_assert(__is_integral(storage_type));
    static_assert(!__is_same(storage_type, bool));
    static_assert(sizeof(storage_type) <= sizeof(U64));

    if constexpr (__is_signed(storage_type)) {
      if constexpr (sizeof(storage_type) == sizeof(U8)) {
        return __builtin_bit_cast(storage_type, read<source, U8>(source_data));
      } else if constexpr (sizeof(storage_type) == sizeof(U16)) {
        return __builtin_bit_cast(storage_type, read<source, U16>(source_data));
      } else if constexpr (sizeof(storage_type) == sizeof(U32)) {
        return __builtin_bit_cast(storage_type, read<source, U32>(source_data));
      } else {
        return __builtin_bit_cast(storage_type, read<source, U64>(source_data));
      }
    } else {
      storage_type value = 0;
      if consteval {
        for (Count index = 0; index < sizeof(storage_type); index++) {
          const Count shift = source == ByteOrder::Little
                                  ? index * 8
                                  : (sizeof(storage_type) - index - 1) * 8;
          value |= storage_type(source_data[index]) << shift;
        }

        return value;
      } else {
        memcpy(&value, source_data, sizeof(storage_type));
        return ensure_endian<source, ByteOrder::Native>(value);
      }
    }
  }

  template <ByteOrder target, typename storage_type>
  static constexpr auto write(storage_type* dest, storage_type value) -> void {
    value = Data::ensure_endian<Data::ByteOrder::Native, target>(value);
    Data::copy(reinterpret_cast<U8*>(dest), &value);
  }

  // Exchanges complete object representations while preserving both lifetimes
  // at their original addresses. Each value must remain valid after receiving
  // the other value's representation, and each destructor later releases the
  // representation held at its address. Runtime performs the exchange through
  // one aligned byte buffer and three byte copies.
  //
  // Constant evaluation expresses the same exchange through value assignment,
  // which is the representation C++ exposes during compilation.
  template <typename type>
  static constexpr auto swap(type& a, type& b) -> void {
    if !consteval {
      alignas(alignof(type)) U8 forgetful[sizeof(type)];
      memcpy(&forgetful, &a, sizeof(type));
      memcpy((void*)&a, &b, sizeof(type));
      memcpy((void*)&b, &forgetful, sizeof(type));
    } else {
      auto temp = a;
      a = b;
      b = temp;
    }
  }
};

}  // namespace Perimortem::Core
