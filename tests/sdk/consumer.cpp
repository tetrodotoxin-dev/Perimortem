// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "tests/data.hpp"

#include <stdio.h>

#include "perimortem/core/bibliotheca.hpp"
#include "perimortem/core/null_terminated.hpp"

#include "perimortem/system/library.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Memory;
using namespace Perimortem::System;

static auto symbol(Library& library, View::Bytes name, Allocator::Arena& errors)
    -> void* {
  return library.symbol(name, errors)
      .visit(
          [](void* address) { return address; },
          [](View::Bytes error) -> void* {
            fprintf(stderr, "%.*s\n", int(error.get_size()), error.get_data());
            return nullptr;
          });
}

// A successful link alone would miss a second allocator hidden in a module.
// Observe both sides of the same thread's counters, then release the provider's
// allocation after unloading its code. Only the runtime owns that allocation.
int main(int argc, const char* argv[]) {
  if (argc != 2) {
    return 1;
  }
  Allocator::Arena errors;
  U8* foreign = nullptr;
  U8* local = nullptr;
  int status;
  {
    auto provider = Library::open(
        Perimortem::Tests::data_path(NullTerminated::to_view(argv[1])), errors);
    status = provider.visit(
        [&](Library& library) {
          auto acquire = reinterpret_cast<U8* (*)()>(
              symbol(library, "acquire"_view, errors));
          auto requests = reinterpret_cast<Count (*)()>(
              symbol(library, "requests"_view, errors));
          if (!acquire || !requests) {
            return 3;
          }
          const Count before = Bibliotheca::check_out_requests();
          local = Bibliotheca::check_out(128).ptr;
          if (requests() != before + 1) {
            return 4;
          }
          foreign = acquire();
          return Bibliotheca::check_out_requests() == before + 2 ? 0 : 5;
        },
        [](View::Bytes error) {
          fprintf(stderr, "%.*s\n", int(error.get_size()), error.get_data());
          return 2;
        });
  }
  if (foreign) {
    Bibliotheca::remit(foreign);
  }
  if (local) {
    Bibliotheca::remit(local);
  }
  return status;
}
