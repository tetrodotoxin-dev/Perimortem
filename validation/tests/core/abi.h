// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#ifndef PERIMORTEM_VALIDATION_CORE_ABI_H
#define PERIMORTEM_VALIDATION_CORE_ABI_H

#include "perimortem/core/access/bytes.h"
#include "perimortem/core/identity/uuid.h"
#include "perimortem/core/view/bytes.h"

C_LINKAGE perimortem_view_bytes
    perimortem_test_view(const U8* data, Count size);
C_LINKAGE perimortem_access_bytes perimortem_test_access(U8* data, Count size);
C_LINKAGE perimortem_uuid perimortem_test_uuid(U64 high, U64 low);

#endif
