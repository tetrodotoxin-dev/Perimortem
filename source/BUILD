# Copyright (c) 2023-present Matt Kaes and contributors
load("@tetro_toolchain//source/bazel:library.bzl", "LINUX", "WEB", "static_library")

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "sources",
    srcs = glob(["**"]),
)

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
