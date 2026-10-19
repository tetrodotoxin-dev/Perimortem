// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/system/process/args.hpp"

#ifdef TOOLCHAIN_LINUX
#include <unistd.h>
#endif

#include "perimortem/core/algorithm/search.hpp"
#include "perimortem/core/data.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/diagnostics/message.hpp"
#include "perimortem/core/math/scalar.hpp"
#include "perimortem/core/storage/pair.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/memory/managed/bytes.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;

static auto basename(View::Bytes path) -> View::Bytes {
  for (Count i = path.get_size(); i > 0; i--) {
    if (path[i - 1] == '/' || path[i - 1] == '\\') {
      return path.slice(i);
    }
  }

  return path;
}

static auto process_name() -> View::Bytes {

#ifdef TOOLCHAIN_LINUX

  thread_local static U8 path_buffer[512];
  S64 size = readlink(
      "/proc/self/exe", Data::cast<char>(path_buffer), sizeof(path_buffer));
  if (size > 0 && Count(size) < sizeof(path_buffer)) {
    return basename(View::Bytes(path_buffer[0], Count(size)));
  }

#endif

  return "process"_view;
}

// Splits one command line argument into the option name and authored value.
// Positional arguments use an empty name. Leading dashes introduce an option.
// Text after the first equals sign becomes its value, while an option without
// an equals sign receives `true`.
static auto parse_argument(View::Bytes argument)
    -> Storage::Pair<View::Bytes, View::Bytes> {
  if (argument.is_empty() || argument[0] != '-') {
    return {View::Bytes(), argument};
  }

  const View::Bytes authored = argument;
  while (!argument.is_empty() && argument[0] == '-') {
    argument = argument.slice(1);
  }

  if (argument.is_empty() || argument[0] == '=') {
    return {authored, "true"_view};
  }

  Count equals = Algorithm::Search::match(argument, "="_view);
  if (equals == Count(-1)) {
    return {argument, "true"_view};
  }

  return {argument.slice(0, equals), argument.slice(equals + 1)};
}

static constexpr auto help_label_size(View::Bytes key) -> Count {
  return key.is_empty() ? "<value>"_view.get_size() : key.get_size() + 1;
}

static auto append_help_label(Managed::Bytes& output, View::Bytes key) -> void {
  output.concat("  "_view);
  if (key.is_empty()) {
    output.concat("<value>"_view);
    return;
  }

  output.append(U8('-'));
  output.concat(key);
}

static auto format_help(
    Allocator::Arena& arena,
    View::Bytes summary,
    const Managed::Map<View::Bytes, View::Bytes>& config,
    View::Bytes command) -> View::Bytes {
  Count label_width = "-help"_view.get_size();
  const auto measure_label = [&label_width](const auto& key, const auto&) {
    if (key == "help"_view) {
      return;
    }

    label_width = Math::Scalar::max(label_width, help_label_size(key));
  };

  config.visit(measure_label);

  label_width += 2;

  Managed::Bytes output{arena};
  output.concat("usage: "_view);
  output.concat(command);
  output.concat(" [arguments]\n\n"_view);
  if (!summary.is_empty()) {
    output.concat(summary);
    output.concat("\n\n"_view);
  }

  output.concat("arguments:\n"_view);

  const auto append_config =
      [&output, label_width](const View::Bytes& key, const View::Bytes& value) {
        if (key == "help"_view) {
          return;
        }

        append_help_label(output, key);
        output.append(U8(' '), label_width - help_label_size(key));
        output.concat(value);
        output.append('\n');
      };

  config.visit(append_config);

  output.concat("  -help"_view);
  output.append(U8(' '), label_width - "-help"_view.get_size());
  output.concat("Show this help.\n"_view);
  return output;
}

auto Process::Args::parse(
    Allocator::Arena& arena,
    const Managed::Map<View::Bytes, View::Bytes>& config,
    View::Vector<View::Bytes> arguments) -> Values {
  Values values(arena);
  const auto* argument_data = arguments.get_data();
  for (Count i = 1; i < arguments.get_size(); i++) {
    View::Bytes authored_argument = arena.copy(argument_data[i]);
    auto argument = parse_argument(authored_argument);
    if (argument.key != "help"_view && !config.contains(argument.key)) {
      Diagnostics::Message<256> error_message(Diagnostics::Log::Level::Error);
      error_message << "unrecognized arg"_view << ' ' << authored_argument;
      return Values(arena);
    }

    const auto append_value =
        [&argument](Managed::Vector<View::Bytes>& grouped_values) {
          grouped_values.insert(argument.value);
        };

    const auto create_values = [&] {
      Managed::Vector<View::Bytes> grouped_values(arena);
      grouped_values.insert(argument.value);
      values.insert(argument.key, Data::take(grouped_values));
    };

    values.find(argument.key).visit(append_value, create_values);
  }

  return values;
}

auto Process::Args::log_help(
    Allocator::Arena& arena,
    View::Bytes summary,
    const Managed::Map<View::Bytes, View::Bytes>& config,
    View::Vector<View::Bytes> arguments) -> void {
  View::Bytes command{};
  if (!arguments.is_empty() && !arguments.get_data()[0].is_empty()) {
    command = basename(arguments.get_data()[0]);
  } else {
    command = process_name();
  }

  Diagnostics::Log::info(format_help(arena, summary, config, command));
}
