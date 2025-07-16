cc_library(
    name = "state",
    srcs = ["state.cpp"],
    hdrs = ["state.hpp"],  # 👈 mark header as part of the library
    copts = ["-std=c++20"],
    linkopts = ["-lfuse3"],
)


cc_binary(
    name = "sealfs",
    srcs = ["main.cpp"],
    deps = [":state"],
    copts = ["-std=c++20"],
    linkopts = ["-lfuse3"],
)


