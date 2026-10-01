// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/memory/dynamic/bytes.hpp"

namespace Perimortem::Tests {

// Bazel may publish data through a manifest rather than a directory tree.
// Outside its test runner, paths remain relative to the repository or bundle.
auto data_path(Perimortem::Core::View::Bytes path)
    -> Perimortem::Memory::Dynamic::Bytes;

}  // namespace Perimortem::Tests
