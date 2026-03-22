add_executable(math_tests math_tests.cpp)
target_link_libraries(math_tests PRIVATE math testing)
target_compile_features(math_tests PRIVATE cxx_std_20)
add_test(NAME math_tests COMMAND math_tests)
