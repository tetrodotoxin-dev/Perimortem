// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/core/bibliotheca.hpp"

using Perimortem::Core::Bibliotheca;

#ifdef PERI_WINDOWS
#define PROVIDER_EXPORT __declspec(dllexport)
#else
#define PROVIDER_EXPORT
#endif

PERIMORTEM_C PROVIDER_EXPORT auto acquire() -> U8* {
  return Bibliotheca::check_out(128).ptr;
}

PERIMORTEM_C PROVIDER_EXPORT auto requests() -> Count {
  return Bibliotheca::check_out_requests();
}
