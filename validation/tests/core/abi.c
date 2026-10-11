// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "validation/tests/core/abi.h"

C_LINKAGE perimortem_view_bytes
    perimortem_test_view(const U8* data, Count size) {
  return (perimortem_view_bytes){data, size};
}

C_LINKAGE perimortem_access_bytes perimortem_test_access(U8* data, Count size) {
  return (perimortem_access_bytes){data, size};
}
