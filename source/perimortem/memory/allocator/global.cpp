// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/memory/allocator/global.hpp"

#include <stdlib.h>

#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/view/null_terminated.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;

auto Allocator::Global::allocate(Count requested_bytes) -> Allocation<U8> {
  if (requested_bytes == 0) {
    return Allocation<U8>();
  }

  if (requested_bytes > CppSize(-1)) {
    Diagnostics::Log::fatal(
        "Global allocation exceeds the platform address range."_view);
  }

  auto* data = Core::Data::cast<U8>(malloc(CppSize(requested_bytes)));
  if (!data) {
    Diagnostics::Log::fatal("Unable to allocate global memory."_view);
  }

  return Allocation<U8>(data, requested_bytes);
}

auto Allocator::Global::release(U8* data) -> void {
  free(data);
}
