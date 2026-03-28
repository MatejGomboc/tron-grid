add_executable(logger_tests
    logger_tests.cpp
)

target_link_libraries(logger_tests PRIVATE logging testing)

add_test(NAME logger_tests COMMAND logger_tests)
set_tests_properties(logger_tests PROPERTIES TIMEOUT 10)
