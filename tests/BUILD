# Copyright (c) 2023-present Matt Kaes and contributors
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@tetro_toolchain//:validation.bzl", "test")

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
        "//:perimortem",
        "@rules_cc//cc/runfiles",
    ],
)

test(
    name = "test",
    srcs = glob(
        [
            "**/*.cpp",
            "**/*.hpp",
        ],
        exclude = [
            "sdk/**",
            "data.cpp",
            "data.hpp",
            "process_support/fixture.cpp",
        ],
    ),
    data = [
        ":data",
        ":process_fixture",
    ],
    deps = [
        ":data_paths",
        "//:perimortem",
        "@tetro_toolchain//source/validation:stream",
        "@tetro_toolchain//source/validation:process",
    ],
)

cc_binary(
    name = "process_fixture",
    testonly = True,
    srcs = ["process_support/fixture.cpp"],
    deps = ["//:perimortem"],
)

cc_binary(
    name = "sdk_provider",
    testonly = True,
    srcs = ["sdk/provider.cpp"],
    linkshared = True,
    tags = ["__DONT_DEPEND_ON_DEF_PARSER__"],
    deps = ["//:perimortem"],
)

cc_test(
    name = "sdk_test",
    srcs = ["sdk/consumer.cpp"],
    args = ["$(rootpath :sdk_provider)"],
    data = [":sdk_provider"],
    deps = [
        ":data_paths",
        "//:perimortem",
    ],
)
