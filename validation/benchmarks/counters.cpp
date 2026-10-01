// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/benchmark.hpp"
#include "perimortem/core/bibliotheca.hpp"

using namespace Perimortem::Core;

// Allocation counts come from the library under measurement. The shared
// runner only samples the counter and has no allocator dependency of its own.
static const bool registered = Toolchain::Validation::Benchmark::register_counter(
    "checkouts/run", Bibliotheca::check_out_requests);
