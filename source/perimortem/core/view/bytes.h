// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#ifndef PERIMORTEM_CORE_VIEW_BYTES_H
#define PERIMORTEM_CORE_VIEW_BYTES_H

#include "toolchain/toolchain.h"

// The ABI represents read only bytes with one pointer and byte count. An empty
// record carries a null pointer and a zero size. Relocating or releasing the
// referenced storage invalidates every copied record.
typedef struct perimortem_view_bytes {
  const U8* data;
  Count size;
} perimortem_view_bytes;

#endif
