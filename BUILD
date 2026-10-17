# Copyright (c) 2023-present Matt Kaes and contributors

load(
    "@tetro_toolchain//:defs.bzl",
    "format",
    "package",
    "snapshot",
    "validation",
    "vscode",
)

# ============                Useful commands                 ============
# Build source using the local machines configuration:
#
#  bazel build //source/...
#
# Check formatting and source policy:
#
#   bazel run :format -- --check
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
# Archive the clean Git commit used to reproduce a release:
#
#   bazel run :snapshot
#

format()
snapshot()
validation()

# The component interfaces share Perimortem's runtime. Keeping that dependency
# private makes each component expose only its declared header dependencies.
package(
    components = {
        "compression": [
            "core",
            "memory",
        ],
        "core": [],
        "memory": [
            "core",
        ],
        "serialization": [
            "core",
            "memory",
        ],
        "system": [
            "core",
            "memory",
            "serialization",
        ],
    },
    module = "perimortem",
    shared = "//source:perimortem_shared",
    static = "//source:perimortem",
)

# Run `bazel run :vscode` to create a VSCode setup for the repository.
vscode()
