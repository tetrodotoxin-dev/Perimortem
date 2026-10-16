// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "toolchain/toolchain.hpp"
#include "toolchain/validation/benchmark.hpp"

#include "perimortem/core/storage/vector.hpp"
#include "perimortem/memory/allocator/arena.hpp"
#include "perimortem/memory/allocator/bibliotheca.hpp"
#include "perimortem/system/random.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;
using namespace Toolchain::Validation;

static Harness AllocatorBench = {
  .name = "Allocators",
};

template <Count alloc_size>
static auto bibliotheca_cycle() -> void {
  Allocator::Bibliotheca::Allocation alloc;
  for (Count i = 0; i < 100; i++) {
    alloc = Allocator::Bibliotheca::check_out(alloc_size);

    U8* data = alloc.get_data();
    Benchmark::prevent_optimization(data);
    Allocator::Bibliotheca::remit(data);
  }
}

template <Count alloc_size>
static auto arena_cycle() -> void {
  Allocator::Arena arena;
  for (Count i = 0; i < 100; i++) {
    auto alloc = arena.allocate(alloc_size);
    Benchmark::prevent_optimization(alloc);
  }
}

#define CYCLE_BENCH(size)                                          \
  VALIDATION_BENCHMARK(AllocatorBench, bibliotheca_cycle_##size) { \
    bibliotheca_cycle<size>();                                     \
  }                                                                \
  VALIDATION_BENCHMARK(AllocatorBench, arena_cycle_##size) {       \
    arena_cycle<size>();                                           \
  }

CYCLE_BENCH(64)
CYCLE_BENCH(1024)
CYCLE_BENCH(65536)

template <Count frame_alloc_count, Count size_minimum, Count size_mask>
static auto frame_stability() -> void {
  Storage::Vector<U8*, frame_alloc_count> ptrs;
  for (Count i = 0; i < frame_alloc_count; i++) {
    auto alloc = Allocator::Bibliotheca::check_out(
        (Random::generate() & size_mask) + size_minimum);
    ptrs[i] = alloc.get_data();
  }

  for (Count i = 0; i < frame_alloc_count; i++) {
    Allocator::Bibliotheca::remit(ptrs[i]);
  }

  Benchmark::prevent_optimization(ptrs[0]);
}

#define STABILITY_BENCH(prefix, count, min_bytes, max_mask)     \
  VALIDATION_BENCHMARK(                                         \
      AllocatorBench, frame_##prefix##_##count##_allocations) { \
    frame_stability<count, min_bytes, max_mask>();              \
  }

STABILITY_BENCH(small, 1024, 64, 0xFF);
STABILITY_BENCH(small, 65536, 64, 0xFF);
STABILITY_BENCH(varied, 1024, 64, 0xFFFF);

// Keeps one fixed allocation window live while each iteration replaces its
// oldest block. This measures reuse under steady allocator churn instead of a
// frame that obtains every block before returning any of them.
template <
    Count frame_alloc_count,
    Count window_count,
    Count size_minimum,
    Count size_mask>
static auto frame_stability_interleaved() -> void {
  constexpr Count window = frame_alloc_count / window_count;
  Storage::Vector<U8*, frame_alloc_count> ptrs;

  for (Count i = 0; i < window; i++) {
    ptrs[i] = Allocator::Bibliotheca::check_out(
                  (Random::generate() & size_mask) + size_minimum)
                  .get_data();
  }

  for (Count i = window; i < frame_alloc_count; i++) {
    ptrs[i] = Allocator::Bibliotheca::check_out(
                  (Random::generate() & size_mask) + size_minimum)
                  .get_data();
    Allocator::Bibliotheca::remit(ptrs[i - window]);
  }

  for (Count i = frame_alloc_count - window; i < frame_alloc_count; i++) {
    Allocator::Bibliotheca::remit(ptrs[i]);
  }

  Benchmark::prevent_optimization(ptrs[0]);
}

#define INTERLEAVED_BENCH(count, windows)                                      \
  VALIDATION_BENCHMARK(                                                        \
      AllocatorBench, frame_##windows##_interleaved##_##count##_allocations) { \
    frame_stability_interleaved<count, windows, 64, 0xFF>();                   \
  }

INTERLEAVED_BENCH(16384, 8);
INTERLEAVED_BENCH(16384, 32);
INTERLEAVED_BENCH(65536, 8);
INTERLEAVED_BENCH(65536, 32);
