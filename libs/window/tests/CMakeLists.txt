add_executable(window_tests
    window_tests.cpp
)

target_link_libraries(window_tests PRIVATE window test_fixture)

add_test(NAME window_tests COMMAND window_tests)
