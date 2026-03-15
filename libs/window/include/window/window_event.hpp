/*
    TronGrid — window event types
    Copyright (C) 2026 Matej Gomboc
    SPDX-Licence-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <cstdint>

namespace WindowLib
{

    //! Tagged-union representing a platform-agnostic window event.
    struct WindowEvent {
        //! Discriminator for the event union.
        enum class Type {
            None,
            Close,
            Resize,
            KeyDown,
            KeyUp,
            MouseMove,
            MouseButtonDown,
            MouseButtonUp,
            Focus,
            Blur
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

        WindowEvent() : type(Type::None), resize{}
        {
        }

        explicit WindowEvent(Type t) : type(t), resize{}
        {
        }
    };

} // namespace WindowLib
