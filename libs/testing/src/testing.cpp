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

#include "testing/testing.hpp"
#include <iostream>
#include <stdexcept>

namespace TestingLib
{

    //! Internal exception thrown when a test check fails.
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

    bool runAll()
    {
        const std::vector<TestCase>& cases = registry();
        std::size_t passed{0};
        std::size_t failed{0};

        for (const TestCase& tc : cases) {
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
        return failed > 0;
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

} // namespace TestingLib
