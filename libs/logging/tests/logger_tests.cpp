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
#include "log/logger.hpp"

#include <thread>

TEST_CASE(logger_construct_destroy)
{
    LoggingLib::Logger logger;
    // Constructor spawns worker, destructor joins — no crash.
}

TEST_CASE(logger_all_severities)
{
    LoggingLib::Logger logger;
    logger.logDebug("Debug message.");
    logger.logInfo("Info message.");
    logger.logWarning("Warning message.");
    logger.logError("Error message.");
    logger.logFatal("Fatal message.");
    // Destructor drains all messages before joining.
}

TEST_CASE(logger_thread_safety)
{
    LoggingLib::Logger logger;
    constexpr int count = 100;

    std::thread t1([&] {
        for (int i = 0; i < count; ++i) {
            logger.logInfo("Thread 1 message.");
        }
    });

    std::thread t2([&] {
        for (int i = 0; i < count; ++i) {
            logger.logWarning("Thread 2 message.");
        }
    });

    t1.join();
    t2.join();
    // Destructor drains remaining messages.
}

int main()
{
    return static_cast<int>(TestFixtureLib::runAll());
}
