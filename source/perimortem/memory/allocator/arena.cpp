// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/allocator/arena.hpp"

#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/math/scalar.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/memory/allocator/bibliotheca.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;

Allocator::Arena::Arena() {
  rented_block = nullptr;

  fetch_page(page_size);
}

Allocator::Arena::~Arena() {
  while (rented_block != nullptr) {
    auto rented = rented_block;
    rented_block = *Data::cast<U8*>(rented_block);
    Allocator::Bibliotheca::remit(rented);
  }
}

auto Allocator::Arena::reset() -> void {
  // Retain the oldest page so the next transaction can allocate without another
  // Bibliotheca checkout. Newer pages are remitted and allocation resumes after
  // the embedded link in the retained page.
  U8* previous = *Data::cast<U8*>(rented_block);
  while (previous != nullptr) {
    auto rented = rented_block;
    rented_block = previous;
    Allocator::Bibliotheca::remit(rented);

    previous = *Data::cast<U8*>(rented_block);
  }

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
      Math::Scalar::max(page_size, bytes_requested + arena_alignment);
  auto alloc = Allocator::Bibliotheca::check_out(alloc_size);

  // The aligned prefix stores a link to the former page. Reset and destruction
  // can then remit every page without a separate allocation for bookkeeping.
  U8** previous = Data::cast<U8*>(alloc.get_data());
  *previous = rented_block;
  rented_block = alloc.get_data();

  // Leave an aligned prefix even when the page link is a smaller pointer.
  usage = arena_alignment;
}
