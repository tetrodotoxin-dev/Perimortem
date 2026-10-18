// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/core/writer/textual.hpp"

namespace Perimortem::Core::Diagnostics {

// Message assembles one diagnostic across conditional writes and submits it
// when the guard leaves scope.
//
// The provided capacity is a hard limit that keeps logging allocation free. If
// a write exceeds that capacity, Message logs a clipping diagnostic at Error
// before submitting any retained message prefix at the requested level. This
// ordering ensures Log reaches the clipping diagnostic before a clipped Fatal
// message enters its termination path.
template <Count capacity>
class Message {
 public:
  Message(
      Log::Level message_level,
      const Source& message_location = Source::current())
      : writer(message_storage.get_access()),
        level(message_level),
        location(message_location) {}

  ~Message() {
    const View::Bytes payload = message_storage.slice(0, writer.get_location());
    if (!writer.is_valid()) {
      constexpr auto notice =
          "Diagnostics::Message clipped a diagnostic because its "
          "inline capacity was exhausted. Increase the Message capacity."_view;
      Log::error(notice, location);
    }

    if (payload.is_empty() && level != Log::Level::Fatal) {
      return;
    }

    Log::log(level, payload, location);
  }

  Message(const Message&) = delete;
  Message(Message&&) = delete;
  auto operator=(const Message&) -> Message& = delete;
  auto operator=(Message&&) -> Message& = delete;

  auto operator<<(View::Bytes value) -> Message& {
    const Count remaining = writer.get_size() - writer.get_location();
    const Count selected =
        value.get_size() < remaining ? value.get_size() : remaining;
    writer << value.slice(0, selected);
    if (selected != value.get_size()) {
      writer << value.slice(selected);
    }

    return *this;
  }

  template <typename value_type>
  auto operator<<(const value_type& value) -> Message& {
    writer << value;
    return *this;
  }

 private:
  Storage::Bytes<capacity> message_storage;
  Writer::Textual writer;
  Log::Level level;
  Source location;
};

}  // namespace Perimortem::Core::Diagnostics
