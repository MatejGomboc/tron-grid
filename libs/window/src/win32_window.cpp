/*
    TronGrid — Win32 window implementation
    Copyright (C) 2026 Matej Gomboc
    SPDX-Licence-Identifier: GPL-3.0-or-later
*/

#ifdef _WIN32

#include "win32_window.hpp"
#include <cstdlib>
#include <iostream>

bool Win32Window::m_class_registered = false;

Win32Window::Win32Window(const WindowConfig& config)
{
    m_hinstance = GetModuleHandle(nullptr);

    // Register window class once
    if (!m_class_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = wndProcStatic;
        wc.hInstance = m_hinstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = CLASS_NAME;

        if (!RegisterClassExW(&wc)) {
            std::cerr << "[TronGrid] Fatal: failed to register Win32 window class\n";
            std::abort();
            return;
        }
        m_class_registered = true;
    }

    // Window style
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!config.resizable) {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }
    if (!config.decorated) {
        style = WS_POPUP;
    }

    // Adjust window rect to account for borders/title bar
    RECT rect = {0, 0, static_cast<LONG>(config.width), static_cast<LONG>(config.height)};
    AdjustWindowRect(&rect, style, FALSE);

    int window_width = rect.right - rect.left;
    int window_height = rect.bottom - rect.top;

    // Centre on primary monitor
    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int screen_height = GetSystemMetrics(SM_CYSCREEN);
    int x = (screen_width - window_width) / 2;
    int y = (screen_height - window_height) / 2;

    // Convert title to wide string
    int title_len = MultiByteToWideChar(CP_UTF8, 0, config.title.c_str(), -1, nullptr, 0);
    std::wstring title_wide(title_len, 0);
    MultiByteToWideChar(CP_UTF8, 0, config.title.c_str(), -1, title_wide.data(), title_len);

    // Set dimensions before CreateWindowExW — WM_SIZE is dispatched during creation
    // and wndProc compares against these to detect real resizes
    m_width = config.width;
    m_height = config.height;

    m_hwnd = CreateWindowExW(0, CLASS_NAME, title_wide.c_str(), style, x, y, window_width, window_height, nullptr, nullptr, m_hinstance,
        this // Pass this pointer for WM_NCCREATE
    );

    if (!m_hwnd) {
        std::cerr << "[TronGrid] Fatal: failed to create Win32 window\n";
        std::abort();
        return;
    }

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
}

Win32Window::~Win32Window()
{
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void Win32Window::pumpEvents()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void* Win32Window::nativeHandle() const
{
    return static_cast<void*>(m_hwnd);
}

void* Win32Window::nativeDisplay() const
{
    return static_cast<void*>(m_hinstance);
}

LRESULT CALLBACK Win32Window::wndProcStatic(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    Win32Window* self = nullptr;

    if (msg == WM_NCCREATE) {
        // Extract and store the this pointer
        auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<Win32Window*>(create_struct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->wndProc(hwnd, msg, wparam, lparam);
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT Win32Window::wndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_CLOSE: {
        WindowEvent ev(WindowEvent::Type::Close);
        pushEvent(ev);
        m_should_close = true;
        return 0;
    }

    case WM_SIZE: {
        uint32_t w = LOWORD(lparam);
        uint32_t h = HIWORD(lparam);
        if (w != m_width || h != m_height) {
            m_width = w;
            m_height = h;
            WindowEvent ev(WindowEvent::Type::Resize);
            ev.resize.width = w;
            ev.resize.height = h;
            pushEvent(ev);
        }
        return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        WindowEvent ev(WindowEvent::Type::KeyDown);
        ev.key.keycode = static_cast<uint32_t>(wparam);
        ev.key.repeat = (lparam & 0x40000000) != 0;
        pushEvent(ev);
        return 0;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP: {
        WindowEvent ev(WindowEvent::Type::KeyUp);
        ev.key.keycode = static_cast<uint32_t>(wparam);
        ev.key.repeat = false;
        pushEvent(ev);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int32_t x = static_cast<int16_t>(LOWORD(lparam));
        int32_t y = static_cast<int16_t>(HIWORD(lparam));

        WindowEvent ev(WindowEvent::Type::MouseMove);
        ev.mouse_move.x = x;
        ev.mouse_move.y = y;
        ev.mouse_move.dx = m_mouse_tracked ? (x - m_last_mouse_x) : 0;
        ev.mouse_move.dy = m_mouse_tracked ? (y - m_last_mouse_y) : 0;
        pushEvent(ev);

        m_last_mouse_x = x;
        m_last_mouse_y = y;
        m_mouse_tracked = true;
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        WindowEvent ev(WindowEvent::Type::MouseButtonDown);
        ev.mouse_button.button = (msg == WM_LBUTTONDOWN) ? 0 : (msg == WM_RBUTTONDOWN) ? 1 : 2;
        ev.mouse_button.x = static_cast<int16_t>(LOWORD(lparam));
        ev.mouse_button.y = static_cast<int16_t>(HIWORD(lparam));
        pushEvent(ev);
        return 0;
    }

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
        WindowEvent ev(WindowEvent::Type::MouseButtonUp);
        ev.mouse_button.button = (msg == WM_LBUTTONUP) ? 0 : (msg == WM_RBUTTONUP) ? 1 : 2;
        ev.mouse_button.x = static_cast<int16_t>(LOWORD(lparam));
        ev.mouse_button.y = static_cast<int16_t>(HIWORD(lparam));
        pushEvent(ev);
        return 0;
    }

    case WM_SETFOCUS: {
        WindowEvent ev(WindowEvent::Type::Focus);
        pushEvent(ev);
        return 0;
    }

    case WM_KILLFOCUS: {
        WindowEvent ev(WindowEvent::Type::Blur);
        pushEvent(ev);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

#endif // _WIN32
