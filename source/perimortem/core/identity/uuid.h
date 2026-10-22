// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#ifndef PERIMORTEM_CORE_IDENTITY_UUID_H
#define PERIMORTEM_CORE_IDENTITY_UUID_H

#include "toolchain/toolchain.h"

// UUID records carry all 128 identifier bits in high and low U64 words. Each
// word uses the host's integer representation. Serialization defines how those
// bits are written as bytes or text for exchange between systems.
typedef struct perimortem_uuid {
  U64 high;
  U64 low;
} perimortem_uuid;

#endif
