# Copyright (c) 2023-present Matt Kaes and contributors
load("@tetro_toolchain//:library.bzl", "LINUX", "WEB", "shared_library")

package(default_visibility = ["//visibility:public"])

config_setting(
    name = "scalar",
    define_values = {"perimortem_avx2": "false"},
)

shared_library(
    name = "perimortem",
    copts = ["-Wreturn-type-c-linkage"],
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
