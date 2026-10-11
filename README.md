# Perimortem

Perimortem is a C++ runtime and utility library for systems programming. It
provides common data types, memory management and operating system services
with control over data layout, allocation and dependencies.

The library uses C++ language features with a small set of C runtime
dependencies. It provides its own containers and algorithms to keep memory
behavior and header dependencies under its control. Basic data types have C
declarations so independently built C and C++ code can exchange them.

## Memory and data

Storage and access are separate concerns. A view lets an operation read
existing data, while an access interface permits changes to that data. Both
borrow storage whose lifetime is established by the caller. This lets the
same operation work with data from different containers and allocators.

Applications choose storage according to the lifetime of their data. Fixed
storage belongs to its containing object, dynamic containers manage their
allocations, and arenas hold data that can be released together. For example,
a parsed document can keep its nodes and strings in one arena and release
that memory when the document is finished.

## Components

- [Core](source/perimortem/core) supplies scalar types, borrowed views,
  algorithms, allocation, diagnostics and threading.
- [Utility](source/perimortem/utility) provides result handling, ranges and
  tables built on Core.
- [Math](source/perimortem/math) describes coordinates, dimensions and boundary
  behavior.
- [Memory](source/perimortem/memory) provides containers, matrices and arenas
  with different storage lifetimes.
- [Compression](source/perimortem/compression) implements compression and
  decompression over byte streams.
- [Serialization](source/perimortem/serialization) reads and writes formats
  including JSON, PNG and Base64, alongside binary and textual streams.
- [System](source/perimortem/system) provides file, path, shared-library and
  other host services.

## Building and testing

Python 3 and Bazel are required. The Bazel version is specified in
[.bazelversion](.bazelversion), and [MODULE.bazel](MODULE.bazel) declares the
Toolchain dependency that supplies the compilers and shared build rules.

Run these commands from the repository root:

```sh
bazel build //source/...
bazel run //validation:tests
bazel run //validation:benchmarks --config=release
bazel build :sdk --config=release
```

Tests and benchmarks are standalone executables. Run them from the repository
root so their test data is available. The SDK includes headers, source, and
static and shared libraries.

For VS Code tasks, debugging and header navigation, run `bazel run :vscode`.

Perimortem is available under the [MIT license](LICENSE).
