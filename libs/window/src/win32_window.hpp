/*
    TronGrid — Win32 window implementation
    Copyright (C) 2026 Matej Gomboc
    SPDX-Licence-Identifier: GPL-3.0-or-later
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

namespace WindowLib
{

class Win32Window : public Window {
public:
    explicit Win32Window(const WindowConfig& config);
    ~Win32Window() override;

    void pumpEvents() override;
    void* nativeHandle() const override;
    void* nativeDisplay() const override;

private:
    static LRESULT CALLBACK wndProcStatic(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    HWND m_hwnd = nullptr;
    HINSTANCE m_hinstance = nullptr;

    // Track last mouse position for deltas
    int32_t m_last_mouse_x = 0;
    int32_t m_last_mouse_y = 0;
    bool m_mouse_tracked = false;

    static constexpr const wchar_t* CLASS_NAME = L"TronGridWindowClass";
    static bool m_class_registered;
};

} // namespace WindowLib

#endif // _WIN32
