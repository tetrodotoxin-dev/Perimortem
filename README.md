# Perimortem

Perimortem is the common C++26 runtime infrastructure for the Tetrodotoxin
family of projects. It supplies shared primitives that belong below individual
products, and Tetrodotoxin's requirements define its primary direction.

As the family grows, a runtime primitive required by multiple projects can move
into Perimortem with one implementation and one contract. Domain runtimes build
on that foundation at their own boundaries. [Photophore](https://github.com/tetrodotoxin-dev/Photophore)
extends Perimortem with windowing and graphical rendering for Tetrodotoxin
applications.

Perimortem is separated into modules with explicit dependencies and SDK
packages. Tetrodotoxin projects can select the modules they require, while
independent projects can use the same slices outside the Tetrodotoxin family.

The C++ runtime is built on lightweight C headers and project owned types. This
keeps the transitive include graph controlled and preserves fast build times as
the runtime grows.

## Building and testing

Python 3 and Bazel are required. The Bazel version is specified in
[.bazelversion](.bazelversion), and [MODULE.bazel](MODULE.bazel) declares the
Toolchain dependency that supplies the compilers and shared build rules.

Run these commands from the repository root:

```sh
bazel run :format -- --check
bazel build //source/...
bazel run :tests
bazel run :benchmarks --config=release
bazel build :sdk --config=release
bazel run :snapshot
```

Tests and benchmarks are standalone executables. Run them from the repository
root so their test data is available. The SDK packages the component headers
and static and shared libraries for independently built consumers. A snapshot
archives the clean Git commit that reproduces the source and validation state.

For VS Code tasks, debugging and header navigation, run `bazel run :vscode`.

Perimortem is available under the [MIT license](LICENSE).
