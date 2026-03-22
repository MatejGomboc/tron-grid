/*
    Copyright (C) 2026 Matej Gomboc https://github.com/MatejGomboc/tron-grid

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
*/

#pragma once

#include <cstddef>
#include <functional>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace TestingLib
{

    //! A registered test case with a name and callable body.
    struct TestCase {
        std::string_view name; //!< Test name.
        std::function<void()> fn; //!< Test body.
    };

    //! Returns the global list of registered test cases.
    std::vector<TestCase>& registry();

    //! Registers a test case; called automatically by TEST_CASE macro.
    void registerTest(std::string_view name, std::function<void()> fn);

    //! Runs all registered tests; returns true if any test failed, false if all passed.
    [[nodiscard]] bool runAll();

    //! Reports a check failure; throws to abort the current test.
    [[noreturn]] void checkFailed(std::string_view expr, std::source_location loc = std::source_location::current());

    //! Reports an equality check failure; throws to abort the current test.
    [[noreturn]] void checkEqualFailed(std::string_view lhs_expr, std::string_view rhs_expr, std::string_view lhs_val, std::string_view rhs_val,
        std::source_location loc = std::source_location::current());

    // ---------------------------------------------------------------------------
    // Template helpers — real logic lives here, not in macros
    // ---------------------------------------------------------------------------

    //! Converts a value to a string for diagnostics.
    template <typename T> [[nodiscard]] std::string toString(const T& v)
    {
        if constexpr (std::is_convertible_v<T, std::string>) {
            return std::string(v);
        } else if constexpr (std::is_arithmetic_v<std::decay_t<T>>) {
            return std::to_string(v);
        } else {
            return "<?>";
        }
    }

    //! Checks that two values are equal; reports failure with stringified expressions and values.
    template <typename A, typename B>
    void checkEqual(const A& lhs, const B& rhs, std::string_view lhs_expr, std::string_view rhs_expr, std::source_location loc = std::source_location::current())
    {
        if (!(lhs == rhs)) {
            checkEqualFailed(lhs_expr, rhs_expr, toString(lhs), toString(rhs), loc);
        }
    }

    //! Checks that a callable throws any exception.
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

} // namespace TestingLib

// ---------------------------------------------------------------------------
// Thin macros — only used for expression stringification (#expr)
// ---------------------------------------------------------------------------

//! Fails with file, line, and stringified expression if `expr` is false.
#define TEST_CHECK(expr)                          \
    do {                                          \
        if (!(expr)) {                            \
            ::TestingLib::checkFailed(#expr); \
        }                                         \
    } while (false)

//! Fails showing both values if `a != b`.
#define TEST_CHECK_EQUAL(a, b) ::TestingLib::checkEqual((a), (b), #a, #b)

//! Fails if `expr` does not throw.
#define TEST_CHECK_THROWS(expr)    \
    ::TestingLib::checkThrows( \
        [&] {                      \
            (void)(expr);          \
        },                         \
        #expr)

//! Defines and auto-registers a test case.
#define TEST_CASE(test_name)                                           \
    static void test_name();                                           \
    namespace                                                          \
    {                                                                  \
        struct test_name##_registrar {                                 \
            test_name##_registrar()                                    \
            {                                                          \
                ::TestingLib::registerTest(#test_name, test_name); \
            }                                                          \
        } test_name##_instance;                                        \
    }                                                                  \
    static void test_name()
