add_executable(math_tests math_tests.cpp)
target_link_libraries(math_tests PRIVATE math testing)
add_test(NAME math_tests COMMAND math_tests)
