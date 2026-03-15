/*
    TronGrid — test fixture
    Copyright (C) 2026 Matej Gomboc
    SPDX-Licence-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <cstddef>
#include <functional>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace TestFixtureLib
{

    struct TestCase {
        std::string_view name; //!< test name
        std::function<void()> fn; //!< test body
    };

    //! returns the global list of registered test cases.
    std::vector<TestCase>& registry();

    //! registers a test case. called automatically by TEST_CASE macro.
    void registerTest(std::string_view name, std::function<void()> fn);

    //! runs all registered tests. returns true if any test failed, false if all passed.
    [[nodiscard]] bool runAll();

    //! reports a check failure. throws to abort the current test.
    [[noreturn]] void checkFailed(std::string_view expr, std::source_location loc = std::source_location::current());

    //! reports an equality check failure. throws to abort the current test.
    [[noreturn]] void checkEqualFailed(std::string_view lhs_expr, std::string_view rhs_expr, std::string_view lhs_val, std::string_view rhs_val,
        std::source_location loc = std::source_location::current());

    // ---------------------------------------------------------------------------
    // Template helpers — real logic lives here, not in macros
    // ---------------------------------------------------------------------------

    //! converts a value to a string for diagnostics.
    template <typename T> std::string toString(const T& v)
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
    void checkEqual(const A& lhs, const B& rhs, std::string_view lhs_expr, std::string_view rhs_expr, std::source_location loc = std::source_location::current())
    {
        if (!(lhs == rhs)) {
            checkEqualFailed(lhs_expr, rhs_expr, toString(lhs), toString(rhs), loc);
        }
    }

    //! checks that a callable throws any exception.
    template <typename Fn> void checkThrows(Fn&& fn, std::string_view expr, std::source_location loc = std::source_location::current())
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
            checkFailed(msg, loc);
        }
    }

} // namespace TestFixtureLib

// ---------------------------------------------------------------------------
// Thin macros — only used for expression stringification (#expr)
// ---------------------------------------------------------------------------

//! fails with file, line, and stringified expression if `expr` is false.
#define TEST_CHECK(expr)                          \
    do {                                          \
        if (!(expr)) {                            \
            ::TestFixtureLib::checkFailed(#expr); \
        }                                         \
    } while (false)

//! fails showing both values if `a != b`.
#define TEST_CHECK_EQUAL(a, b) ::TestFixtureLib::checkEqual((a), (b), #a, #b)

//! fails if `expr` does not throw.
#define TEST_CHECK_THROWS(expr)    \
    ::TestFixtureLib::checkThrows( \
        [&] {                      \
            (void)(expr);          \
        },                         \
        #expr)

//! defines and auto-registers a test case.
#define TEST_CASE(test_name)                                           \
    static void test_name();                                           \
    namespace                                                          \
    {                                                                  \
        struct test_name##_registrar {                                 \
            test_name##_registrar()                                    \
            {                                                          \
                ::TestFixtureLib::registerTest(#test_name, test_name); \
            }                                                          \
        } test_name##_instance;                                        \
    }                                                                  \
    static void test_name()
