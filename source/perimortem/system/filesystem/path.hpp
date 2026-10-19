// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/access/bytes.hpp"
#include "perimortem/core/data.hpp"
#include "perimortem/core/storage/option.hpp"
#include "perimortem/core/storage/result.hpp"
#include "perimortem/core/view/bytes.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/core/view/vector.hpp"
#include "perimortem/memory/global/vector.hpp"

namespace Perimortem::System::Filesystem {

// Path gives filesystem callers one canonical language for selecting host
// locations without exposing native drive, separator or working directory
// syntax. Anchor bindings are captured when Perimortem loads, so later working
// directory and environment changes do not retarget an existing Path:
//
//  [root]:/  `/` on POSIX or the Windows volume or share containing [work].
//  [temp]:/  The host temporary directory.
//  [home]:/  The current user's home directory.
//  [work]:/  The process working directory.
//  [exec]:/  The directory containing the executable image.
//
// Components use UTF 8 and `/`. Empty components, a trailing separator, `.` and
// `..` are rejected so one location has one lexical representation. Preventing
// parent navigation removes lexical anchor escape, but links remain host
// filesystem objects. Following one can reach any location permitted by the
// host and carries no Path confinement guarantee.
//
// Path stores an Anchor and a View over the component bytes without allocating.
// The caller keeps those bytes alive and unchanged through every use. Storing
// only that View keeps Path trivially copyable and permits constexpr
// validation. A system that retains a Path instead copies the component bytes,
// as Directory does.
//
// Canonical serialization can use caller storage. Native emission necessarily
// allocates because it combines the components with a captured host anchor,
// terminates the result and, on Windows, converts UTF 8 to the extended length
// UTF 16 form. The returned Native object exposes that materialization to both
// Perimortem transactions and foreign host APIs so conversion policy has one
// public owner.
class Path {
 public:
  enum class Anchor : U8 {
    Root,
    Temp,
    Home,
    Work,
    Exec,
  };

  enum class Failure : U8 {
    InvalidAnchor,
    InvalidPath,
    InvalidEncoding,
  };

  enum class EmissionFailure : U8 {
    AnchorUnavailable,
    HostLimit,
    UnsupportedHost,
  };

#ifdef TOOLCHAIN_WINDOWS

  using NativeUnit = wchar_t;

#else

  using NativeUnit = char;

#endif

  // Native retains one null terminated host path in process heap storage. Its
  // size excludes the terminator and counts host units: bytes on POSIX and
  // wchar_t values on Windows. Moving transfers the allocation and leaves the
  // source without a native string. Copying is rejected so an additional host
  // path allocation remains explicit. The data pointer remains valid until the
  // Native object is moved or destroyed.
  class Native {
    friend Path;

   public:
    Native(const Native&) = delete;
    auto operator=(const Native&) -> Native& = delete;
    Native(Native&& source) = default;
    auto operator=(Native&& source) -> Native& = default;
    ~Native() = default;

    auto get_data() const -> const NativeUnit* { return values.get_data(); }
    auto get_size() const -> Count {
      return values.is_empty() ? Count(0) : values.get_size() - 1;
    }

    auto get_view() const -> Core::View::Vector<NativeUnit> {
      return values.get_view().slice(0, get_size());
    }

   private:
    Native(Memory::Global::Vector<NativeUnit>&& source)
        : values(Core::Data::take(source)) {}

    Memory::Global::Vector<NativeUnit> values;
  };

  Path() = delete;
  constexpr Path(const Path&) = default;

  // Validates components for a known anchor without parsing serialized anchor
  // text. Empty components select the anchor directory itself. None reports an
  // unsupported Anchor value, invalid UTF 8 or a noncanonical component
  // sequence.
  static constexpr auto create(Anchor anchor, Core::View::Bytes components)
      -> Core::Storage::Option<Path> {
    if (!valid_anchor(anchor) || !valid_utf8(components) ||
        !valid_components(components)) {
      return {};
    }

    return Path(anchor, components);
  }

  // Parses the complete `[name]:/components` form. Failure distinguishes an
  // unsupported prefix, invalid UTF 8 and component grammar so authored path
  // diagnostics can explain which contract was rejected.
  static constexpr auto parse(Core::View::Bytes canonical)
      -> Core::Storage::Result<Path, Failure> {
    if (canonical.get_size() < anchor_prefix_size) {
      return Failure::InvalidAnchor;
    }

    const Core::View::Bytes prefix = canonical.slice(0, anchor_prefix_size);
    if (!valid_prefix(prefix)) {
      return Failure::InvalidAnchor;
    }

    const Core::View::Bytes components = canonical.slice(anchor_prefix_size);
    if (!valid_utf8(components)) {
      return Failure::InvalidEncoding;
    }

    if (!valid_components(components)) {
      return Failure::InvalidPath;
    }

    return Path(anchor_from_prefix(prefix), components);
  }

  // Literal validation rejects a bad canonical path during compilation and
  // retains a View over the literal's static storage.
  template <Core::View::NullTerminated::Literal canonical>
  static consteval auto literal() -> Path {
    constexpr Core::View::Bytes text = canonical.get_view();
    static_assert(
        valid_serialized(text),
        "Path literals require a supported anchor and canonical UTF-8 path");
    return Path(
        anchor_from_prefix(text.slice(0, anchor_prefix_size)),
        text.slice(anchor_prefix_size));
  }

  // Serializes the anchor prefix followed by component bytes and returns a View
  // over exactly that canonical text. It has no null terminator and remains
  // accessible until `output` changes or becomes inaccessible. Insufficient
  // capacity returns None without modifying `output`.
  constexpr auto serialize(Core::Access::Bytes output) const
      -> Core::Storage::Option<Core::View::Bytes> {
    const Count required_size = get_serialized_size();
    if (output.get_size() < required_size) {
      return {};
    }

    const Core::View::Bytes prefix = get_prefix(anchor);
    for (Count index = 0; index < prefix.get_size(); index++) {
      output.get_data()[index] = prefix[index];
    }

    for (Count index = 0; index < components.get_size(); index++) {
      output.get_data()[prefix.get_size() + index] = components[index];
    }

    return output.get_view().slice(0, required_size);
  }

  // Resolves the captured host location for the selected anchor into an exact
  // process heap allocation. The result has storage independent of Path and can
  // cross thread boundaries. Windows writes UTF 16 with its extended length
  // prefix. Other supported hosts write their native byte path. Failure leaves
  // no partial native string.
  EXPORTED(PERIMORTEM) auto emit() const
      -> Core::Storage::Result<Native, EmissionFailure>;

  constexpr auto get_anchor() const -> Anchor { return anchor; }
  constexpr auto get_components() const -> Core::View::Bytes {
    return components;
  }

  constexpr auto get_anchor_name() const -> Core::View::Bytes {
    return get_prefix(anchor).slice(1, 4);
  }

  constexpr auto get_serialized_size() const -> Count {
    return anchor_prefix_size + components.get_size();
  }

  // Returns the final component without assigning file or directory identity.
  // An anchor root has no name and returns an empty View.
  constexpr auto get_name() const -> Core::View::Bytes {
    Count name_start = 0;
    for (Count index = 0; index < components.get_size(); index++) {
      if (components[index] == '/') {
        name_start = index + 1;
      }
    }

    return components.slice(name_start);
  }

  // Returns the lexical parent while retaining the same anchor. An anchor root
  // has no parent. The returned Path references the same component storage, so
  // the caller keeps those bytes accessible through every use of either Path.
  constexpr auto try_parent() const -> Core::Storage::Option<Path> {
    if (components.is_empty()) {
      return {};
    }

    Count parent_size = 0;
    for (Count index = 0; index < components.get_size(); index++) {
      if (components[index] == '/') {
        parent_size = index;
      }
    }

    return Path(anchor, components.slice(0, parent_size));
  }

  // Extension is a lexical property of the final component. The returned View
  // begins after its last interior period and excludes that separator. A period
  // at either boundary does not establish an extension.
  constexpr auto get_extension() const -> Core::View::Bytes {
    const Core::View::Bytes name = get_name();
    for (Count index = name.get_size(); index > 0; index--) {
      if (name[index - 1] == '.' && index - 1 > 0 && index < name.get_size()) {
        return name.slice(index);
      }
    }

    return {};
  }

  constexpr auto operator==(const Path& rhs) const -> Bool {
    return anchor == rhs.anchor && components == rhs.components;
  }

 private:
  static constexpr Count anchor_prefix_size = 8;

  constexpr Path(Anchor anchor, Core::View::Bytes components)
      : anchor(anchor), components(components) {}

  static constexpr auto valid_anchor(Anchor candidate) -> Bool {
    switch (candidate) {
    case Anchor::Root:
    case Anchor::Temp:
    case Anchor::Home:
    case Anchor::Work:
    case Anchor::Exec:
      return True;
    }

    return False;
  }

  static constexpr auto get_prefix(Anchor candidate) -> Core::View::Bytes {
    switch (candidate) {
    case Anchor::Root:
      return "[root]:/"_view;
    case Anchor::Temp:
      return "[temp]:/"_view;
    case Anchor::Home:
      return "[home]:/"_view;
    case Anchor::Work:
      return "[work]:/"_view;
    case Anchor::Exec:
      return "[exec]:/"_view;
    }

    return {};
  }

  static constexpr auto valid_prefix(Core::View::Bytes prefix) -> Bool {
    return prefix == "[root]:/"_view || prefix == "[temp]:/"_view ||
           prefix == "[home]:/"_view || prefix == "[work]:/"_view ||
           prefix == "[exec]:/"_view;
  }

  static constexpr auto anchor_from_prefix(Core::View::Bytes prefix) -> Anchor {
    if (prefix == "[temp]:/"_view) {
      return Anchor::Temp;
    }

    if (prefix == "[home]:/"_view) {
      return Anchor::Home;
    }

    if (prefix == "[work]:/"_view) {
      return Anchor::Work;
    }

    if (prefix == "[exec]:/"_view) {
      return Anchor::Exec;
    }

    return Anchor::Root;
  }

  static constexpr auto valid_utf8(Core::View::Bytes text) -> Bool {
    Count index = 0;
    while (index < text.get_size()) {
      const U8 first = text[index++];
      if (first <= 0x7F) {
        continue;
      }

      Count continuation_count = 0;
      U8 second_minimum = 0x80;
      U8 second_maximum = 0xBF;
      if (first >= 0xC2 && first <= 0xDF) {
        continuation_count = 1;
      } else if (first >= 0xE0 && first <= 0xEF) {
        continuation_count = 2;
        if (first == 0xE0) {
          second_minimum = 0xA0;
        } else if (first == 0xED) {
          second_maximum = 0x9F;
        }
      } else if (first >= 0xF0 && first <= 0xF4) {
        continuation_count = 3;
        if (first == 0xF0) {
          second_minimum = 0x90;
        } else if (first == 0xF4) {
          second_maximum = 0x8F;
        }
      } else {
        return False;
      }

      if (continuation_count > text.get_size() - index) {
        return False;
      }

      const U8 second = text[index++];
      if (second < second_minimum || second > second_maximum) {
        return False;
      }

      for (Count continuation = 1; continuation < continuation_count;
           continuation++) {
        const U8 byte = text[index++];
        if (byte < 0x80 || byte > 0xBF) {
          return False;
        }
      }
    }

    return True;
  }

  static constexpr auto valid_components(Core::View::Bytes source) -> Bool {
    if (source.is_empty()) {
      return True;
    }

    Count component_start = 0;
    for (Count index = 0; index <= source.get_size(); index++) {
      const Bool boundary = index == source.get_size() || source[index] == '/';
      if (!boundary) {
        if (source[index] == '\\' || source[index] == 0) {
          return False;
        }

        continue;
      }

      const Count component_size = index - component_start;
      if (component_size == 0) {
        return False;
      }

      const Bool current =
          component_size == 1 && source[component_start] == '.';
      const Bool parent = component_size == 2 &&
                          source[component_start] == '.' &&
                          source[component_start + 1] == '.';
      if (current || parent) {
        return False;
      }

      component_start = index + 1;
    }

    return True;
  }

  static constexpr auto valid_serialized(Core::View::Bytes canonical) -> Bool {
    if (canonical.get_size() < anchor_prefix_size) {
      return False;
    }

    const Core::View::Bytes prefix = canonical.slice(0, anchor_prefix_size);
    const Core::View::Bytes source = canonical.slice(anchor_prefix_size);
    return valid_prefix(prefix) && valid_utf8(source) &&
           valid_components(source);
  }

  Anchor anchor;
  Core::View::Bytes components;
};

}  // namespace Perimortem::System::Filesystem

// Canonical Path literals retain the string literal storage and fail during
// compilation when their anchor, UTF-8 or component grammar is invalid.
template <Perimortem::Core::View::NullTerminated::Literal canonical>
consteval auto operator""_path() -> Perimortem::System::Filesystem::Path {
  return Perimortem::System::Filesystem::Path::literal<canonical>();
}
