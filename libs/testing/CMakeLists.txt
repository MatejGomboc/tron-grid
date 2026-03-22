add_library(testing STATIC
    src/testing.cpp
)

target_include_directories(testing
    PUBLIC include/
    PRIVATE src/
)

target_compile_features(testing PUBLIC cxx_std_20)

add_subdirectory(tests)
