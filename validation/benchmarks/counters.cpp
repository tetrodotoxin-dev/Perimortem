// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/validation/benchmark.hpp"

#include "perimortem/memory/allocator/bibliotheca.hpp"

using namespace Perimortem::Memory;
using namespace Toolchain::Validation;

// Allocation counts come from the library under measurement. The shared
// runner only samples the counter and has no allocator dependency of its own.
static const bool registered = Benchmark::register_counter(
    "checkouts/run",
    Allocator::Bibliotheca::check_out_requests);
