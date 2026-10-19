// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#ifndef PERIMORTEM_CORE_ACCESS_BYTES_H
#define PERIMORTEM_CORE_ACCESS_BYTES_H

#include "toolchain/toolchain.h"

// The ABI represents writable bytes with one pointer and byte count. A valid
// record has a nonnull pointer even when the count is zero. Relocating or
// releasing the referenced storage invalidates every copied record.
typedef struct perimortem_access_bytes {
  U8* data;
  Count size;
} perimortem_access_bytes;

#endif
