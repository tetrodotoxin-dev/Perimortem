# Copyright (c) 2023-present Matt Kaes and contributors

load("@tetro_toolchain//:defs.bzl", "benchmarks", "package", "tests", "vscode")

# ============                Useful commands                 ============
# Build source using the local machines configuration:
#
#  bazel build //source/...
#
# Build and run all tests using the local machines configuration:
#
#   bazel run :tests
#
# Build and run all tests using the local machines configuration:
#
#   bazel run :tests
#
# Build and run all benchmarks locally:
#
#   bazel run :benchmarks
#
# One shot command to fully package SDK for all supported platforms:
#
#   bazel build :sdk --config=release
#

# The component interfaces share Perimortem's runtime. Keeping that dependency
# private makes each component expose only its declared header dependencies.
package(
    components = [
        "core",
        "utility",
        "math",
        "memory",
        "compression",
        "serialization",
        "system",
    ],
    module = "perimortem",
    shared = "//source:perimortem_shared",
    static = "//source:perimortem",
)

# Run `bazel run :vscode` to create a VSCode setup for the repository.
vscode()

# SDK unit tests are built as an independent binary so they don't depend on Bazel's
# machinery. Test can be build on one machine and run on another as long as the test
# data folder is provided.
#
# All Bazel commands should be run from the repository root.
tests(
    srcs = glob([
        "validation/unit_tests/**/*.cpp",
        "validation/unit_tests/**/*.hpp",
    ]),
    deps = [":perimortem"],
)

benchmarks(
    srcs = glob(["validation/benchmarks/**/*.cpp"]),
    deps = [":perimortem"],
)
