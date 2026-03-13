add_executable(test_fixture_self_tests
    test_fixture_self_tests.cpp
)

target_link_libraries(test_fixture_self_tests PRIVATE test_fixture)

add_test(NAME test_fixture_self_tests COMMAND test_fixture_self_tests)
