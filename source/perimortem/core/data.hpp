// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "toolchain/toolchain.hpp"

// The target C runtime supplies these declarations, including their import and
// exception specifications. Clang recognizes the same memory intrinsics without
// redeclaring a different C boundary in each host configuration.
#include <string.h>

namespace Perimortem::Core {

// Data provides storage neutral operations over object representations. These
// operations align locations, copy bytes, adapt byte order, or expose a
// requested type without allocating or releasing caller storage.
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

  // Rounds the provided offset up to the next multiple of the requested
  // alignment. Alignment values 0 and 1 return the offset unchanged through a
  // compile time branch.
  template <Count alignment_size>
  static constexpr auto align(Count offset) -> Count {
    if constexpr (alignment_size <= 1) {
      return offset;
    }

    constexpr Count alignment_filter = alignment_size - 1;
    // Powers of two can align with a mask instead of division. Subtracting one
    // leaves every lower bit set, so its intersection with the original value
    // is zero only for a single set bit. The earlier branch handles zero and
    // one before this test.
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

  // Reinterprets caller storage without constructing `target_type`. The caller
  // establishes the target lifetime and alignment before dereferencing.
  template <typename target_type>
  static auto cast(const void* source) -> const target_type* {
    // TODO: start_lifetime_as
    return reinterpret_cast<const target_type*>(source);
  }

  // Reinterprets writable caller storage without constructing `target_type`.
  // The caller establishes the target lifetime and alignment before use.
  template <typename target_type>
  static auto cast(void* source) -> target_type* {
    // TODO: start_lifetime_as
    return reinterpret_cast<target_type*>(source);
  }

  // Downcasts a class pointer while preserving constant evaluation. The source
  // type must be a base of the requested target, so this overload cannot
  // reinterpret unrelated storage.
  template <typename target_type, typename source_type>
    requires(__is_base_of(source_type, target_type))
  static constexpr auto cast(source_type* source) -> target_type* {
    return static_cast<target_type*>(source);
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

  // Returns an rvalue reference to `source`. Move construction or assignment
  // can then transfer its state without creating an intermediate object.
  template <typename storage_type>
  static constexpr auto take(storage_type& source) -> storage_type&& {
    return static_cast<storage_type&&>(source);
  }

  // Converts equal size values by copying their object representation without
  // invoking a numeric or converting constructor.
  template <typename target_type, typename source_type>
  static constexpr auto bit_cast(source_type source) -> target_type {
    static_assert(sizeof(target_type) == sizeof(source_type));
    return __builtin_bit_cast(target_type, source);
  }

  // Serializes a data or function pointer through the compiler address width.
  // The value can be restored only within the process that supplied it.
  template <typename pointer_type>
    requires(__is_pointer(pointer_type))
  static auto address(pointer_type source) -> CppSize {
    return reinterpret_cast<CppSize>(source);
  }

  // Restores a pointer serialized by `address`. No storage or lifetime is
  // created, so the original pointer contract still governs dereferencing.
  template <typename pointer_type>
    requires(__is_pointer(pointer_type))
  static auto restore(CppSize source) -> pointer_type {
    return reinterpret_cast<pointer_type>(source);
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
