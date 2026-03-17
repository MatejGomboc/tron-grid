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

#pragma once

#include <cstdint>

namespace WindowLib
{

    //! Tagged-union representing a platform-agnostic window event.
    struct WindowEvent {
        //! Discriminator for the event union.
        enum class Type {
            None, //!< No event.
            Close, //!< Window close requested.
            Resize, //!< Window resized.
            KeyDown, //!< Key pressed.
            KeyUp, //!< Key released.
            MouseMove, //!< Mouse cursor moved.
            MouseButtonDown, //!< Mouse button pressed.
            MouseButtonUp, //!< Mouse button released.
            Focus, //!< Window gained focus.
            Blur //!< Window lost focus.
        };

        Type type = Type::None; //!< Event type discriminator.

        union {
            struct {
                uint32_t width; //!< New width in pixels.
                uint32_t height; //!< New height in pixels.
            } resize; //!< New dimensions after resize.

            struct {
                uint32_t keycode; //!< Platform-specific key code.
                bool repeat; //!< True if this is a key-repeat event.
            } key; //!< Keyboard event data.

            struct {
                int32_t x; //!< Cursor x position.
                int32_t y; //!< Cursor y position.
                int32_t dx; //!< Horizontal delta since last event.
                int32_t dy; //!< Vertical delta since last event.
            } mouse_move; //!< Mouse movement event data.

            struct {
                uint8_t button; //!< Button index (0=left, 1=right, 2=middle).
                int32_t x; //!< Cursor x position.
                int32_t y; //!< Cursor y position.
            } mouse_button; //!< Mouse button event data.
        };

        //! Default constructor; initialises to Type::None with zeroed union.
        WindowEvent() :
            type(Type::None), resize{}
        {
        }

        //! Constructs an event with the given type and zeroed union.
        explicit WindowEvent(Type t) :
            type(t), resize{}
        {
        }
    };

} // namespace WindowLib
