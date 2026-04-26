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

#include <concepts>
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

    //! Converts a value to a string for diagnostics. Resolution order:
    //! - Anything convertible to std::string (so const char*, std::string_view, etc.).
    //! - Built-in arithmetic types via std::to_string.
    //! - Project / user types that provide an ADL-discoverable to_string overload —
    //!   e.g. `std::string to_string(const MyType&)` in MyType's namespace lets
    //!   TEST_CHECK_EQUAL print informative diagnostics for failed comparisons of
    //!   project types instead of the previous "<?>" placeholder.
    //! - Fallback: "<?>".
    //!
    //! The ADL lookup uses a `using std::to_string` declaration so it picks up both
    //! standard-library overloads and any user-provided overload found through
    //! argument-dependent lookup.
    template <typename T> [[nodiscard]] std::string toString(const T& v)
    {
        if constexpr (std::is_convertible_v<T, std::string>) {
            return std::string(v);
        } else if constexpr (std::is_arithmetic_v<std::decay_t<T>>) {
            return std::to_string(v);
        } else if constexpr (requires(const T& x) {
                                 { to_string(x) } -> std::convertible_to<std::string>;
                             }) {
            using std::to_string; // Brings std overloads into scope; ADL finds user ones.
            return to_string(v);
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

// MSVC C4127: "conditional expression is constant" on do { } while(false) — standard macro pattern.
#ifdef _MSC_VER
#define TEST_CHECK_SUPPRESS_BEGIN __pragma(warning(push)) __pragma(warning(disable : 4127))
#define TEST_CHECK_SUPPRESS_END __pragma(warning(pop))
#else
#define TEST_CHECK_SUPPRESS_BEGIN
#define TEST_CHECK_SUPPRESS_END
#endif

//! Fails with file, line, and stringified expression if `expr` is false.
#define TEST_CHECK(expr)                      \
    TEST_CHECK_SUPPRESS_BEGIN                 \
    do {                                      \
        if (!(expr)) {                        \
            ::TestingLib::checkFailed(#expr); \
        }                                     \
    } while (false) TEST_CHECK_SUPPRESS_END

//! Fails showing both values if `a != b`.
#define TEST_CHECK_EQUAL(a, b) ::TestingLib::checkEqual((a), (b), #a, #b)

//! Fails if `expr` does not throw.
#define TEST_CHECK_THROWS(expr) \
    ::TestingLib::checkThrows(  \
        [&] {                   \
            (void)(expr);       \
        },                      \
        #expr)

//! Defines and auto-registers a test case.
#define TEST_CASE(test_name)                                       \
    static void test_name();                                       \
    namespace                                                      \
    {                                                              \
        struct test_name##_registrar {                             \
            test_name##_registrar()                                \
            {                                                      \
                ::TestingLib::registerTest(#test_name, test_name); \
            }                                                      \
        } test_name##_instance;                                    \
    }                                                              \
    static void test_name()
