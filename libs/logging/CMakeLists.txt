add_library(logging STATIC
    src/logger.cpp
)

target_include_directories(logging
    PUBLIC  include/
    PRIVATE src/
)

target_compile_features(logging PUBLIC cxx_std_20)

target_link_libraries(logging PUBLIC signals)

add_subdirectory(tests)
