/*
 * TronGrid — test fixture
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace test_fixture
{

    struct TestCase {
        std::string_view name;
        std::function<void()> fn;
    };

    /// Returns the global list of registered test cases.
    std::vector<TestCase>& registry();

    /// Registers a test case. Called automatically by TEST_CASE macro.
    void register_test(std::string_view name, std::function<void()> fn);

    /// Runs all registered tests. Returns 0 if all pass, 1 if any fail.
    int run_all();

    /// Reports a check failure. Throws to abort the current test.
    [[noreturn]] void check_failed(std::string_view expr, std::string_view file, int line);

    /// Reports an equality check failure. Throws to abort the current test.
    [[noreturn]] void check_equal_failed(std::string_view lhs_expr, std::string_view rhs_expr, std::string_view lhs_val, std::string_view rhs_val, std::string_view file,
        int line);

} // namespace test_fixture

// ---------------------------------------------------------------------------
// Macros
// ---------------------------------------------------------------------------

/// Fails with file, line, and stringified expression if `expr` is false.
#define TEST_CHECK(expr)                                             \
    do {                                                             \
        if (!(expr))                                                 \
            ::test_fixture::check_failed(#expr, __FILE__, __LINE__); \
    } while (false)

/// Fails showing both values if `a != b`.
#define TEST_CHECK_EQUAL(a, b)                                                                            \
    do {                                                                                                  \
        const auto& lhs_ = (a);                                                                           \
        const auto& rhs_ = (b);                                                                           \
        if (!(lhs_ == rhs_)) {                                                                            \
            auto to_str_ = [](const auto& v) -> std::string {                                             \
                if constexpr (std::is_convertible_v<decltype(v), std::string>)                            \
                    return std::string(v);                                                                \
                else if constexpr (std::is_arithmetic_v<std::decay_t<decltype(v)>>)                       \
                    return std::to_string(v);                                                             \
                else                                                                                      \
                    return "<?>";                                                                         \
            };                                                                                            \
            ::test_fixture::check_equal_failed(#a, #b, to_str_(lhs_), to_str_(rhs_), __FILE__, __LINE__); \
        }                                                                                                 \
    } while (false)

/// Fails if `expr` does not throw.
#define TEST_CHECK_THROWS(expr)                                                       \
    do {                                                                              \
        bool threw_ = false;                                                          \
        try {                                                                         \
            (void)(expr);                                                             \
        } catch (...) {                                                               \
            threw_ = true;                                                            \
        }                                                                             \
        if (!threw_)                                                                  \
            ::test_fixture::check_failed(#expr " did not throw", __FILE__, __LINE__); \
    } while (false)

/// Defines and auto-registers a test case.
#define TEST_CASE(test_name)                                          \
    static void test_name();                                          \
    namespace                                                         \
    {                                                                 \
        struct test_name##_registrar {                                \
            test_name##_registrar()                                   \
            {                                                         \
                ::test_fixture::register_test(#test_name, test_name); \
            }                                                         \
        } test_name##_instance;                                       \
    }                                                                 \
    static void test_name()
