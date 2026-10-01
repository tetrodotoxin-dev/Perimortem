// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/allocator/arena.hpp"

#include "perimortem/core/bibliotheca.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/math.hpp"
#include "perimortem/core/null_terminated.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;

Allocator::Arena::Arena() {
  rented_block = nullptr;

  fetch_page(page_size);
}

Allocator::Arena::~Arena() {
  while (rented_block != nullptr) {
    auto rented = rented_block;
    rented_block = *Core::Data::cast<U8*>(rented_block);
    Bibliotheca::remit(rented);
  }
}

auto Allocator::Arena::reset() -> void {
  // Return all blocks we've rented from the Bibliotheca until we only have a
  // single block left.
  U8* previous = *Core::Data::cast<U8*>(rented_block);
  while (previous != nullptr) {
    auto rented = rented_block;
    rented_block = previous;
    Bibliotheca::remit(rented);

    previous = *Core::Data::cast<U8*>(rented_block);
  }

  // Reset our usage since we are reusing the last block.
  usage = arena_alignment;
}

auto Allocator::Arena::fetch_page(Count bytes_requested) -> void {
  // The page link and final bump alignment both need room. Bibliotheca sees
  // only the resulting byte count, so reject arithmetic overflow here first.
  constexpr Count overhead = arena_alignment + arena_alignment - 1;
  if (bytes_requested > Count(-1) - overhead) {
    Diagnostics::Log::fatal("Arena allocation size overflow."_view);
  }

  const Count alloc_size =
      Math::max(page_size, bytes_requested + arena_alignment);
  auto alloc = Core::Bibliotheca::check_out(alloc_size);

  // Store the previous pointer in the arena itself.
  U8** previous = Core::Data::cast<U8*>(alloc.ptr);

  // Store and swap the blocks.
  *previous = rented_block;
  rented_block = alloc.ptr;

  // Leave an aligned prefix even when the page link is a smaller pointer.
  usage = arena_alignment;
}
