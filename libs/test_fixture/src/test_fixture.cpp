/*
 * TronGrid — test fixture
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#include "test_fixture/test_fixture.hpp"

#include <iostream>
#include <stdexcept>

namespace test_fixture
{

    struct CheckFailure : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    std::vector<TestCase>& registry()
    {
        static std::vector<TestCase> cases;
        return cases;
    }

    void register_test(std::string_view name, std::function<void()> fn)
    {
        registry().push_back({name, std::move(fn)});
    }

    int run_all()
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

    [[noreturn]] void check_failed(std::string_view expr, std::string_view file, int line)
    {
        std::string msg;
        msg += file;
        msg += ":";
        msg += std::to_string(line);
        msg += ": check failed: ";
        msg += expr;
        throw CheckFailure(msg);
    }

    [[noreturn]] void check_equal_failed(std::string_view lhs_expr, std::string_view rhs_expr, std::string_view lhs_val, std::string_view rhs_val, std::string_view file,
        int line)
    {
        std::string msg;
        msg += file;
        msg += ":";
        msg += std::to_string(line);
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

} // namespace test_fixture
