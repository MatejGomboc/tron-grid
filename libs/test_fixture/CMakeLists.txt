add_library(test_fixture STATIC
    src/test_fixture.cpp
)

target_include_directories(test_fixture
    PUBLIC include/
    PRIVATE src/
)

target_compile_features(test_fixture PUBLIC cxx_std_20)

add_subdirectory(tests)
