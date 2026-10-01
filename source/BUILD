# Copyright (c) 2023-present Matt Kaes and contributors

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@tetro_toolchain//source/bazel:library.bzl", "LINUX", "WEB", "static_library")

package(default_visibility = ["//visibility:public"])

# Defines all ot he modules and cross module dependencies used for SDK generation.
_COMPONENTS = {
    "core": [],
    "utility": ["core"],
    "math": [
        "core",
        "utility",
    ],
    "memory": [
        "core",
        "math",
        "utility",
    ],
    "compression": [
        "core",
        "memory",
    ],
    "serialization": [
        "core",
        "memory",
    ],
    "system": [
        "core",
        "memory",
        "utility",
    ],
}

# These declarations follow public header dependencies. Implementation uses,
# such as Serialization calling Compression, belong to the common runtime.
[
    cc_library(
        name = component,
        hdrs = glob(
            [
                "perimortem/" + component + "/**/*.h",
                "perimortem/" + component + "/**/*.hpp",
            ],
            allow_empty = True,
        ),
        includes = ["."],
        deps = [":" + dependency for dependency in dependencies],
    )
    for component, dependencies in _COMPONENTS.items()
]

filegroup(
    name = "sources",
    srcs = glob(["**"]),
)

# Used to build a scalar only version of Perimortem with AVX2 disabled to provide
# even older CPU support.
config_setting(
    name = "scalar",
    define_values = {"perimortem_avx2": "false"},
)

static_library(
    name = "perimortem",
    srcs = glob(["**/*.cpp"]),
    hdrs = glob([
        "**/*.hpp",
        "**/*.h",
    ]),
    includes = ["."],
    linkopts = select({
        LINUX: ["-lpthread"],
        "//conditions:default": [],
    }),
    local_defines = select({
        ":scalar": [],
        WEB: [],
        "//conditions:default": ["PERI_AVX2"],
    }),
)
