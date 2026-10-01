# Copyright (c) 2023-present Matt Kaes and contributors

load("@tetro_toolchain//:defs.bzl", "LINUX", "WEB", "WINDOWS", "library")

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

_LIBRARY = dict(
    srcs = glob(["perimortem/**/*.cpp"]),
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

library(
    srcs = _LIBRARY["srcs"],
    hdrs = _LIBRARY["hdrs"],
    components = _COMPONENTS,
    includes = _LIBRARY["includes"],
    linkopts = _LIBRARY["linkopts"],
    local_defines = _LIBRARY["local_defines"],
)

library(
    name = "perimortem_shared",
    srcs = _LIBRARY["srcs"],
    hdrs = _LIBRARY["hdrs"],
    includes = _LIBRARY["includes"],
    linkage = "shared",
    linkopts = _LIBRARY["linkopts"],
    local_defines = _LIBRARY["local_defines"],
    shared_lib_name = select({
        WINDOWS: "perimortem.dll",
        "//conditions:default": "libperimortem.so",
    }),
)
