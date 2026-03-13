add_library(signal INTERFACE)

target_include_directories(signal
    INTERFACE include/
)

target_compile_features(signal INTERFACE cxx_std_20)

add_subdirectory(tests)
