// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/memory/dynamic/bytes.hpp"
#include "perimortem/memory/matrix.hpp"
#include "perimortem/serialization/rgba8.hpp"

namespace Perimortem::Serialization {

// Png maps encoded chunks to and from a Matrix of four byte Rgba8 samples.
// Matrix owns row storage and Domain extents. The codec expands source channels
// and reverses filters on decode, then selects filters and compresses on encode.
// Sample bytes retain their decoded values, and absent alpha becomes opaque.
// No transfer conversion occurs here.
class Png {
 public:
  // Decodes a PNG byte stream into a row major matrix of Rgba8 samples. Other
  // supported source channel layouts are expanded before publication.
  //
  // Returns an empty matrix on malformed or unsupported input.
  //
  // CRC 32 validation is skipped in release builds to improve throughput.
  //
  // Benchmarked on AMD Ryzen 9 9950X3D, Arch Linux 6.19.11, using libpng 1.6.58
  // as a reference (CRC disabled for parity):
  //
  //   Fixture       Perimortem   libpng decode
  //   gradient        134 MB/s         63 MB/s
  //   complex         100 MB/s        100 MB/s
  //   noise           288 MB/s        265 MB/s
  //   icon             51 MB/s         51 MB/s
  //   mandelbrot       43 MB/s        	30 MB/s
  //   zoneplate       143 MB/s        105 MB/s
  //
  static EXPORTED(PERIMORTEM) auto decode(Core::View::Bytes source)
      -> Memory::Matrix<Rgba8>;

  // Encodes an Rgba8 matrix to a PNG byte stream. Adaptive filtering scores each
  // row under all five filter types and writes the lowest scoring choice.
  //
  // Returns empty bytes for bad inputs and empty matrices.
  //
  // For compression Level::Default is used for all PNGs as it produces the most
  // well rounded results.
  //
  // Benchmarked on AMD Ryzen 9 9950X3D, Arch Linux 6.19.11, using libpng 1.6.58
  // as a reference using level 6 compression:
  //
  //   Fixture       Perimortem   libpng encode     Compression Ratios
  //   gradient         48 MB/s          7 MB/s       0.99 (smaller)
  //   complex          29 MB/s          5 MB/s       1.05 (bigger)
  //   noise            43 MB/s         22 MB/s       1.02 (bigger)
  //   icon             55 MB/s          9 MB/s       1.01 (bigger)
  //   mandelbrot       16 MB/s          3 MB/s       1.04 (much bigger)
  //   zoneplate        33 MB/s          7 MB/s       1.40 (much bigger)
  //
  static EXPORTED(PERIMORTEM) auto encode(const Memory::Matrix<Rgba8>& image)
      -> Memory::Dynamic::Bytes;
};

}  // namespace Perimortem::Serialization
