// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/toolchain.hpp"
#include "toolchain/validation/benchmark.hpp"

#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/core/writer/textual.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/serialization/formats/json/blueprint.hpp"
#include "perimortem/serialization/formats/json/node.hpp"
#include "perimortem/system/filesystem/file.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;
using namespace Perimortem::Serialization;
using namespace Toolchain::Validation;

static Storage::Bytes<1 << 15> json_data;
static Writer::Textual json_text(json_data);
static constexpr Count json_batch = 1024;

static auto load_json(View::Bytes source_path) -> void {
  auto path =
      Filesystem::Path::create(Filesystem::Path::Anchor::Work, source_path);
  const auto load_source = [](const Filesystem::Path& selected) {
    auto source = Filesystem::File::read(selected);
    source.visit(
        [](const Global::Bytes& contents) {
          json_text.set_pointer(0);
          json_text << contents;
        },

        [] {
          Diagnostics::Log::fatal("Unable to load JSON benchmark source."_view);
        });
  };

  const auto invalid_path = [] {
    Diagnostics::Log::fatal("JSON benchmark source path is invalid."_view);
  };

  path.visit(load_source, invalid_path);
}

static Harness JsonBlueprint = {
  .name = "Json",
};

VALIDATION_BENCHMARK(JsonBlueprint, blueprint_x1024) {
  Count size = 0;
  Allocator::Arena arena;
  for (Count i = 0; i < json_batch; i++) {
    const Formats::Json::Blueprint result_members[] = {
      {"name"_view, "ttx"_view},
      {"version"_view, "1.0"_view},
    };

    const Formats::Json::Blueprint members[] = {
      {"jsonrpc"_view, "2.0"_view},
      {"id"_view, 1},
      {"result"_view, Formats::Json::Blueprint::object(result_members)},
    };

    Formats::Json::Node node =
        Formats::Json::Blueprint::object(members).construct(arena);

    size += node.get_size();
    arena.reset();
  }

  Benchmark::prevent_optimization(size);
}

static Harness JsonSmall = {
  .name = "Json",
  .init = []() { load_json("validation/data/json/test.json"_view); },
};

VALIDATION_BENCHMARK(JsonSmall, small_x1024) {
  Count size = 0;

  Allocator::Arena arena;
  for (Count i = 0; i < json_batch; i++) {
    Formats::Json::Node node;
    node.parse(arena, json_text);
    size += node.get_size();
    arena.reset();
  }

  Benchmark::prevent_optimization(size);
}

static Harness JsonTokenizeRpc = {
  .name = "Json",
  .init = []() { load_json("validation/data/json/tokenize_rpc.json"_view); },
};

VALIDATION_BENCHMARK(JsonTokenizeRpc, tokenize_rpc_x1024) {
  Count size = 0;

  Allocator::Arena arena;
  for (Count i = 0; i < json_batch; i++) {
    Formats::Json::Node node;
    node.parse(arena, json_text);
    size += node.get_size();
    arena.reset();
  }

  Benchmark::prevent_optimization(size);
}

static Harness JsonInitRpc = {
  .name = "Json",
  .init = []() { load_json("validation/data/json/init_rpc.json"_view); },
};

VALIDATION_BENCHMARK(JsonInitRpc, jsonrpc_init_x1024) {
  Count size = 0;

  Allocator::Arena arena;
  for (Count i = 0; i < json_batch; i++) {
    Formats::Json::Node node;
    node.parse(arena, json_text);
    size += node.get_size();
    arena.reset();
  }

  Benchmark::prevent_optimization(size);
}
