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
    return static_cast<int>(TestingLib::runAll());
}
