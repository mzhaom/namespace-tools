package(default_visibility = ["//visibility:public"])
licenses(["notice"])  # Apache 2.0

cc_binary(
  name = "namespace-sandbox",
  srcs = select({
    "//conditions:default": ["namespace-sandbox.c"],
  }),
  copts = ["-std=c99"],
)

cc_binary(
  name = "new-network-namespace",
  srcs = [
    "new-network-namespace.c",
  ],
  copts = ["-std=c99"],
)
