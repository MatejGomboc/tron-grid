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
