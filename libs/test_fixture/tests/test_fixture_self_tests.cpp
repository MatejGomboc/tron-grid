/*
    TronGrid — test fixture self-tests
    Copyright (C) 2026 Matej Gomboc
    SPDX-Licence-Identifier: GPL-3.0-or-later
*/

#include "test_fixture/test_fixture.hpp"

#include <stdexcept>

TEST_CASE(check_true_passes)
{
    TEST_CHECK(true);
    TEST_CHECK(1 == 1);
    TEST_CHECK(42 > 0);
}

TEST_CASE(check_equal_same_values)
{
    TEST_CHECK_EQUAL(1, 1);
    TEST_CHECK_EQUAL(0, 0);
    TEST_CHECK_EQUAL(-5, -5);
}

TEST_CASE(check_throws_catches_exception)
{
    TEST_CHECK_THROWS(throw std::runtime_error("expected"));
    TEST_CHECK_THROWS(throw 42);
}

int main()
{
    return static_cast<int>(TestFixtureLib::runAll());
}
