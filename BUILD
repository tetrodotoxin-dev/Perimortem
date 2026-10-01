# Copyright (c) 2023-present Matt Kaes and contributors

load("@tetro_toolchain//source/bazel:library.bzl", "static_library")
load("@tetro_toolchain//source/bazel:package.bzl", "package_release")
load("@tetro_toolchain//source/bazel:validation.bzl", "benchmarks", "tests")
load("@tetro_toolchain//source/bazel:vscode.bzl", "vscode")

package(default_visibility = ["//visibility:public"])

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

# Run `bazel build :vscode` to create a VSCode setup for the repository.
vscode(name = "vscode")

# The component interfaces share Perimortem's runtime. Keeping that dependency
# private makes each component expose only its declared header dependencies.
_COMPONENTS = [
    "core",
    "utility",
    "math",
    "memory",
    "compression",
    "serialization",
    "system",
]

[
    static_library(
        name = component,
        implementation_deps = ["//source:perimortem"],
        deps = ["//source:" + component],
    )
    for component in _COMPONENTS
]

alias(
    name = "perimortem",
    actual = "//source:perimortem",
)

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

package_release(
    name = "sdk",
    components = {"//source:" + component: component for component in _COMPONENTS},
    static = "//source:perimortem",
)
