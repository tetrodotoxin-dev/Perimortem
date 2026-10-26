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
        ],
    },
    module = "perimortem",
    shared = "//source:perimortem_shared",
    static = "//source:perimortem",
)

format()

snapshot()

# Native validation links the shared runtime so a missing public export fails
# before SDK publication. Web validation links the static runtime because the
# hosted shared module requires an embedding application. The same suite
# therefore exercises both supported linkage boundaries.
validation(
    web_deps = ["//source:perimortem"],
    deps = ["//source:perimortem_shared"],
)

# Run `bazel run :vscode` to create a VSCode setup for the repository.
vscode()
