# Copyright (c) 2023-present Matt Kaes and contributors
load("@tetro_toolchain//:validation.bzl", "benchmark")

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "sources",
    srcs = glob(["**"]),
)

benchmark(
    name = "run",
    srcs = glob(["**/*.cpp"]),
    data = ["//tests:data"],
    deps = [
        "//:perimortem",
        "//tests:data_paths",
    ],
)
