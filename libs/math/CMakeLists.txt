add_library(math INTERFACE)
target_include_directories(math INTERFACE include)
target_compile_features(math INTERFACE cxx_std_20)

add_subdirectory(tests)
