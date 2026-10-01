# Copyright (c) 2023-present Matt Kaes and contributors
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@tetro_toolchain//source/bazel:validation.bzl", "benchmark", "test")

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "sources",
    srcs = glob(["**"]),
)

filegroup(
    name = "data",
    srcs = glob(["data/**"]),
)

cc_library(
    name = "data_paths",
    testonly = True,
    srcs = ["data.cpp"],
    hdrs = ["data.hpp"],
    deps = [
        "//source:perimortem",
        "@rules_cc//cc/runfiles",
    ],
)

test(
    name = "unit_tests",
    srcs = glob([
        "unit_tests/**/*.cpp",
        "unit_tests/**/*.hpp",
    ]) + [
        "log.cpp",
        "log.hpp",
    ],
    data = [":data"],
    deps = [
        ":data_paths",
        "//source:perimortem",
        "@tetro_toolchain//source/validation:stream",
    ],
)

benchmark(
    name = "benchmarks",
    srcs = glob(["benchmarks/**/*.cpp"]),
    data = [":data"],
    deps = [
        ":data_paths",
        "//source:perimortem",
    ],
)
