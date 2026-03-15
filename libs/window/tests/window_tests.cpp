/*
    TronGrid — window library tests
    Copyright (C) 2026 Matej Gomboc
    SPDX-Licence-Identifier: GPL-3.0-or-later
*/

#include "test_fixture/test_fixture.hpp"
#include "window/window_event.hpp"

TEST_CASE(default_event_is_none)
{
    WindowLib::WindowEvent ev;
    TEST_CHECK(ev.type == WindowLib::WindowEvent::Type::None);
}

TEST_CASE(explicit_event_type)
{
    WindowLib::WindowEvent ev(WindowLib::WindowEvent::Type::Close);
    TEST_CHECK(ev.type == WindowLib::WindowEvent::Type::Close);
}

TEST_CASE(resize_event_data)
{
    WindowLib::WindowEvent ev(WindowLib::WindowEvent::Type::Resize);
    ev.resize.width = 1920;
    ev.resize.height = 1080;
    TEST_CHECK_EQUAL(ev.resize.width, static_cast<uint32_t>(1920));
    TEST_CHECK_EQUAL(ev.resize.height, static_cast<uint32_t>(1080));
}

TEST_CASE(key_event_data)
{
    WindowLib::WindowEvent ev(WindowLib::WindowEvent::Type::KeyDown);
    ev.key.keycode = 27;
    ev.key.repeat = false;
    TEST_CHECK_EQUAL(ev.key.keycode, static_cast<uint32_t>(27));
    TEST_CHECK(!ev.key.repeat);
}

int main()
{
    return static_cast<int>(TestFixtureLib::runAll());
}
