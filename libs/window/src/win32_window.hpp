/*
 * TronGrid — Win32 window implementation
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#pragma once

#ifdef _WIN32

#include "window/window.hpp"
// Lean and mean Windows
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

class Win32Window : public Window {
public:
    explicit Win32Window(const WindowConfig& config);
    ~Win32Window() override;

    void pump_events() override;
    VkSurfaceKHR create_surface(VkInstance instance) override;

    HWND handle() const
    {
        return hwnd_;
    }
    HINSTANCE instance() const
    {
        return hinstance_;
    }

private:
    static LRESULT CALLBACK wnd_proc_static(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    HWND hwnd_ = nullptr;
    HINSTANCE hinstance_ = nullptr;

    // Track last mouse position for deltas
    int32_t last_mouse_x_ = 0;
    int32_t last_mouse_y_ = 0;
    bool mouse_tracked_ = false;

    static constexpr const wchar_t* CLASS_NAME = L"TronGridWindowClass";
    static bool class_registered_;
};

#endif // _WIN32
