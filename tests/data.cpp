// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "tests/data.hpp"

#include <memory>
#include <stdlib.h>

#include "rules_cc/cc/runfiles/runfiles.h"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace rules_cc::cc::runfiles;

// Test data belongs to this repository, not the shared validation SDK. Keep
// Bazel's manifest support here so shipping the harness adds no runfiles ABI.
auto Perimortem::Tests::data_path(View::Bytes path) -> Dynamic::Bytes {
  const char* workspace = getenv("TEST_WORKSPACE");
  if (!workspace) {
    return Dynamic::Bytes(path);
  }
  std::unique_ptr<Runfiles> files(Runfiles::CreateForTest());
  if (!files) {
    return Dynamic::Bytes();
  }
  std::string name(workspace);
  name += '/';
  name.append(reinterpret_cast<const char*>(path.get_data()), path.get_size());
  const auto location = files->Rlocation(name);
  return Dynamic::Bytes(
      View::Bytes(
          reinterpret_cast<const U8*>(location.data()), location.size()));
}
