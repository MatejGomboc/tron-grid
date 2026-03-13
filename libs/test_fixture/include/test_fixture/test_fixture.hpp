/*
 * TronGrid — test fixture
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstddef>
#include <functional>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace test_fixture
{

    struct TestCase {
        std::string_view name; //!< test name
        std::function<void()> fn; //!< test body
    };

    //! returns the global list of registered test cases.
    std::vector<TestCase>& registry();

    //! registers a test case. called automatically by TEST_CASE macro.
    void register_test(std::string_view name, std::function<void()> fn);

    //! runs all registered tests. returns 0 if all pass, 1 if any fail.
    [[nodiscard]] int run_all();

    //! reports a check failure. throws to abort the current test.
    [[noreturn]] void check_failed(std::string_view expr, std::source_location loc = std::source_location::current());

    //! reports an equality check failure. throws to abort the current test.
    [[noreturn]] void check_equal_failed(std::string_view lhs_expr, std::string_view rhs_expr, std::string_view lhs_val, std::string_view rhs_val,
        std::source_location loc = std::source_location::current());

    // ---------------------------------------------------------------------------
    // Template helpers — real logic lives here, not in macros
    // ---------------------------------------------------------------------------

    //! converts a value to a string for diagnostics.
    template <typename T> std::string to_string(const T& v)
    {
        if constexpr (std::is_convertible_v<T, std::string>) {
            return std::string(v);
        } else if constexpr (std::is_arithmetic_v<std::decay_t<T>>) {
            return std::to_string(v);
        } else {
            return "<?>";
        }
    }

    //! checks that two values are equal; reports failure with stringified expressions and values.
    template <typename A, typename B>
    void check_equal(const A& lhs, const B& rhs, std::string_view lhs_expr, std::string_view rhs_expr, std::source_location loc = std::source_location::current())
    {
        if (!(lhs == rhs)) {
            check_equal_failed(lhs_expr, rhs_expr, to_string(lhs), to_string(rhs), loc);
        }
    }

    //! checks that a callable throws any exception.
    template <typename Fn> void check_throws(Fn&& fn, std::string_view expr, std::source_location loc = std::source_location::current())
    {
        bool threw = false;
        try {
            fn();
        } catch (...) {
            threw = true;
        }
        if (!threw) {
            std::string msg(expr);
            msg += " did not throw";
            check_failed(msg, loc);
        }
    }

} // namespace test_fixture

// ---------------------------------------------------------------------------
// Thin macros — only used for expression stringification (#expr)
// ---------------------------------------------------------------------------

//! fails with file, line, and stringified expression if `expr` is false.
#define TEST_CHECK(expr)                         \
    do {                                         \
        if (!(expr)) {                           \
            ::test_fixture::check_failed(#expr); \
        }                                        \
    } while (false)

//! fails showing both values if `a != b`.
#define TEST_CHECK_EQUAL(a, b) ::test_fixture::check_equal((a), (b), #a, #b)

//! fails if `expr` does not throw.
#define TEST_CHECK_THROWS(expr)   \
    ::test_fixture::check_throws( \
        [&] {                     \
            (void)(expr);         \
        },                        \
        #expr)

//! defines and auto-registers a test case.
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
