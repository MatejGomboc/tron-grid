add_library(signals INTERFACE)

target_include_directories(signals
    INTERFACE include/
)

target_compile_features(signals INTERFACE cxx_std_20)

add_subdirectory(tests)
