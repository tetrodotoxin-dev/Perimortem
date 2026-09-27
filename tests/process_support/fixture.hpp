// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/memory/dynamic/bytes.hpp"

namespace Perimortem::Tests {

// The test binary and child fixture are sibling build outputs. Use the running
// executable's directory so this works through Bazel and from a copied bundle.
auto fixture_path() -> Perimortem::Memory::Dynamic::Bytes;

}  // namespace Perimortem::Tests
