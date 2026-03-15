/*
    TronGrid — window event types
    Copyright (C) 2026 Matej Gomboc
    SPDX-Licence-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <cstdint>

struct WindowEvent {
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

    Type type = Type::None;

    union {
        struct {
            uint32_t width;
            uint32_t height;
        } resize;

        struct {
            uint32_t keycode;
            bool repeat;
        } key;

        struct {
            int32_t x;
            int32_t y;
            int32_t dx; // delta since last event
            int32_t dy;
        } mouse_move;

        struct {
            uint8_t button; // 0=left, 1=right, 2=middle
            int32_t x;
            int32_t y;
        } mouse_button;
    };

    WindowEvent() : type(Type::None), resize{}
    {
    }

    explicit WindowEvent(Type t) : type(t), resize{}
    {
    }
};
