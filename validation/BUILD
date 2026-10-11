# Copyright (c) 2023-present Matt Kaes and contributors

load("@tetro_toolchain//:defs.bzl", "benchmarks", "tests")

filegroup(
    name = "sources",
    srcs = glob([
        "benchmarks/**",
        "data/**",
        "tests/**",
    ]) + ["BUILD"],
    visibility = ["//visibility:public"],
)

tests(deps = ["//:perimortem"])

benchmarks(deps = ["//:perimortem"])
