cc_library(
    name = "state",
    srcs = ["state.cpp"],
    hdrs = ["state.hpp", "common.hpp"],
    copts = ["-std=c++20"],
    linkopts = ["-lfuse3"],
)

cc_library(
    name = "ll_ops",
    srcs = ["ll_ops.cpp"],
    hdrs = ["ll_ops.hpp", "common.hpp"],
    deps = [":state"],
    copts = ["-std=c++20"],
    linkopts = ["-lfuse3"],
)



cc_binary(
    name = "sealfs",
    srcs = ["main.cpp"],
    deps = [":state", ":ll_ops"],
    copts = ["-std=c++20"],
    linkopts = ["-lfuse3"],
)


