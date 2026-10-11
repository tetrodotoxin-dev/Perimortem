// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/global/bytes.hpp"

#include <stdlib.h>

#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/null_terminated.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;

auto Global::Bytes::allocate(Count requested_bytes) -> Allocator::Allocation {
  if (requested_bytes == 0) {
    return Allocator::Allocation();
  }

  if (requested_bytes > CppSize(-1)) {
    Diagnostics::Log::fatal(
        "Global allocation exceeds the platform address range."_view);
  }

  auto* data = static_cast<U8*>(malloc(CppSize(requested_bytes)));
  if (!data) {
    Diagnostics::Log::fatal("Unable to allocate global memory."_view);
  }

  return Allocator::Allocation{.ptr = data, .capacity = requested_bytes};
}

auto Global::Bytes::release(U8* data) -> void {
  free(data);
}
