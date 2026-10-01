# Copyright (c) 2023-present Matt Kaes and contributors
load("@tetro_toolchain//source/bazel:package.bzl", "package_release")

package(default_visibility = ["//visibility:public"])

alias(
    name = "perimortem",
    actual = "//source:perimortem",
)

alias(
    name = "build",
    actual = "//source:perimortem",
)

package_release(
    name = "sdk",
    target = "//source:perimortem",
)
