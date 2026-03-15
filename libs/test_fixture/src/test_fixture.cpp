/*
 * TronGrid — test fixture
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#include "test_fixture/test_fixture.hpp"

#include <iostream>
#include <stdexcept>

namespace TestFixtureLib
{

    struct CheckFailure : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    std::vector<TestCase>& registry()
    {
        static std::vector<TestCase> cases;
        return cases;
    }

    void registerTest(std::string_view name, std::function<void()> fn)
    {
        registry().push_back({name, std::move(fn)});
    }

    int runAll()
    {
        const auto& cases = registry();
        std::size_t passed = 0;
        std::size_t failed = 0;

        for (const auto& tc : cases) {
            try {
                tc.fn();
                std::cout << "  PASS  " << tc.name << "\n";
                ++passed;
            } catch (const CheckFailure& e) {
                std::cout << "  FAIL  " << tc.name << "\n";
                std::cout << "        " << e.what() << "\n";
                ++failed;
            } catch (const std::exception& e) {
                std::cout << "  FAIL  " << tc.name << " (unhandled exception)\n";
                std::cout << "        " << e.what() << "\n";
                ++failed;
            }
        }

        std::cout << "\n" << passed << " passed, " << failed << " failed, " << cases.size() << " total\n";

        return failed > 0 ? 1 : 0;
    }

    [[noreturn]] void checkFailed(std::string_view expr, std::source_location loc)
    {
        std::string msg;
        msg += loc.file_name();
        msg += ":";
        msg += std::to_string(loc.line());
        msg += ": check failed: ";
        msg += expr;
        throw CheckFailure(msg);
    }

    [[noreturn]] void checkEqualFailed(std::string_view lhs_expr, std::string_view rhs_expr, std::string_view lhs_val, std::string_view rhs_val, std::source_location loc)
    {
        std::string msg;
        msg += loc.file_name();
        msg += ":";
        msg += std::to_string(loc.line());
        msg += ": check equal failed: ";
        msg += lhs_expr;
        msg += " == ";
        msg += rhs_expr;
        msg += " (";
        msg += lhs_val;
        msg += " != ";
        msg += rhs_val;
        msg += ")";
        throw CheckFailure(msg);
    }

} // namespace TestFixtureLib
