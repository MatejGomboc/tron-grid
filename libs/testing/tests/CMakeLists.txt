add_executable(testing_self_tests
    testing_self_tests.cpp
)

target_link_libraries(testing_self_tests PRIVATE testing)

add_test(NAME testing_self_tests COMMAND testing_self_tests)
