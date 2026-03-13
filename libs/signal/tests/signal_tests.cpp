/*
 * TronGrid — signal library tests
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#include "test_fixture/test_fixture.hpp"
#include "signal/signal.hpp"

#include <memory>
#include <thread>

TEST_CASE(emit_and_consume)
{
    signal::Signal<int> sig;
    sig.emit(42);

    int value = 0;
    TEST_CHECK(sig.consume(value));
    TEST_CHECK_EQUAL(value, 42);
}

TEST_CASE(consume_empty_returns_false)
{
    signal::Signal<int> sig;

    int value = 0;
    TEST_CHECK(!sig.consume(value));
}

TEST_CASE(fifo_order)
{
    signal::Signal<int> sig;
    sig.emit(1);
    sig.emit(2);
    sig.emit(3);

    int value = 0;
    sig.consume(value);
    TEST_CHECK_EQUAL(value, 1);
    sig.consume(value);
    TEST_CHECK_EQUAL(value, 2);
    sig.consume(value);
    TEST_CHECK_EQUAL(value, 3);
}

TEST_CASE(empty_and_size)
{
    signal::Signal<int> sig;
    TEST_CHECK(sig.empty());
    TEST_CHECK_EQUAL(sig.size(), static_cast<std::size_t>(0));

    sig.emit(1);
    TEST_CHECK(!sig.empty());
    TEST_CHECK_EQUAL(sig.size(), static_cast<std::size_t>(1));
}

TEST_CASE(weak_ptr_ownership_model)
{
    auto sig = std::make_shared<signal::Signal<int>>();
    std::weak_ptr<signal::Signal<int>> weak = sig;

    sig->emit(99);
    TEST_CHECK(!weak.expired());

    sig.reset();
    TEST_CHECK(weak.expired());
}

TEST_CASE(thread_safety)
{
    signal::Signal<int> sig;
    constexpr int count = 1000;

    std::thread producer([&] {
        for (int i = 0; i < count; ++i)
            sig.emit(i);
    });

    std::thread consumer([&] {
        int consumed = 0;
        while (consumed < count)
        {
            int value = 0;
            if (sig.consume(value))
                ++consumed;
        }
    });

    producer.join();
    consumer.join();

    TEST_CHECK(sig.empty());
}

int main()
{
    return test_fixture::run_all();
}
