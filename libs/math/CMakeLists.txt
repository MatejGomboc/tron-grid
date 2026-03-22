add_library(math INTERFACE)
target_include_directories(math INTERFACE include)

add_subdirectory(tests)
