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
#include "signal/signal.hpp"
#include <memory>
#include <thread>

TEST_CASE(emit_and_consume)
{
    SignalsLib::Signal<int> sig;
    sig.emit(42);

    int value = 0;
    TEST_CHECK(sig.consume(value));
    TEST_CHECK_EQUAL(value, 42);
}

TEST_CASE(consume_empty_returns_false)
{
    SignalsLib::Signal<int> sig;

    int value = 0;
    TEST_CHECK(!sig.consume(value));
}

TEST_CASE(fifo_order)
{
    SignalsLib::Signal<int> sig;
    sig.emit(1);
    sig.emit(2);
    sig.emit(3);

    int value = 0;
    (void)sig.consume(value);
    TEST_CHECK_EQUAL(value, 1);
    (void)sig.consume(value);
    TEST_CHECK_EQUAL(value, 2);
    (void)sig.consume(value);
    TEST_CHECK_EQUAL(value, 3);
}

TEST_CASE(empty_and_size)
{
    SignalsLib::Signal<int> sig;
    TEST_CHECK(sig.empty());
    TEST_CHECK_EQUAL(sig.size(), static_cast<std::size_t>(0));

    sig.emit(1);
    TEST_CHECK(!sig.empty());
    TEST_CHECK_EQUAL(sig.size(), static_cast<std::size_t>(1));
}

TEST_CASE(weak_ptr_ownership_model)
{
    auto sig = std::make_shared<SignalsLib::Signal<int>>();
    std::weak_ptr<SignalsLib::Signal<int>> weak = sig;

    sig->emit(99);
    TEST_CHECK(!weak.expired());

    sig.reset();
    TEST_CHECK(weak.expired());
}

TEST_CASE(thread_safety)
{
    SignalsLib::Signal<int> sig;
    constexpr int count = 1000;

    std::thread producer([&] {
        for (int i = 0; i < count; ++i) {
            sig.emit(i);
        }
    });

    std::thread consumer([&] {
        int consumed = 0;
        while (consumed < count) {
            int value = 0;
            if (sig.consume(value)) {
                ++consumed;
            }
        }
    });

    producer.join();
    consumer.join();

    TEST_CHECK(sig.empty());
}

int main()
{
    return static_cast<int>(TestFixtureLib::runAll());
}
