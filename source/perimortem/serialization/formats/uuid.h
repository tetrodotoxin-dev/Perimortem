// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#ifndef PERIMORTEM_SERIALIZATION_FORMATS_UUID_H
#define PERIMORTEM_SERIALIZATION_FORMATS_UUID_H

#include "toolchain/toolchain.h"

// The ABI stores a UUID as high and low words. High holds the first sixteen
// hexadecimal digits and low holds the final sixteen.
typedef struct perimortem_uuid {
  U64 high;
  U64 low;
} perimortem_uuid;

#endif
