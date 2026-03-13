/*
 * TronGrid — Win32 window implementation
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#ifdef _WIN32

#include "window/win32_window.hpp"
#include <stdexcept>

bool Win32Window::class_registered_ = false;

Win32Window::Win32Window(const WindowConfig& config)
{
    hinstance_ = GetModuleHandle(nullptr);

    // Register window class once
    if (!class_registered_) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = wnd_proc_static;
        wc.hInstance = hinstance_;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = CLASS_NAME;

        if (!RegisterClassExW(&wc)) {
            throw std::runtime_error("Failed to register Win32 window class");
        }
        class_registered_ = true;
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

    hwnd_ = CreateWindowExW(0, CLASS_NAME, title_wide.c_str(), style, x, y, window_width, window_height, nullptr, nullptr, hinstance_,
        this // Pass this pointer for WM_NCCREATE
    );

    if (!hwnd_) {
        throw std::runtime_error("Failed to create Win32 window");
    }

    width_ = config.width;
    height_ = config.height;

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
}

Win32Window::~Win32Window()
{
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void Win32Window::pump_events()
{
    MSG msg;
    while (PeekMessageW(&msg, hwnd_, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

VkSurfaceKHR Win32Window::create_surface(VkInstance /*instance*/)
{
    // TODO(etape-2): implement with Volk once Vulkan loading is integrated
    throw std::runtime_error("Vulkan surface creation not yet implemented (requires Volk)");
}

LRESULT CALLBACK Win32Window::wnd_proc_static(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
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
        return self->wnd_proc(hwnd, msg, wparam, lparam);
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT Win32Window::wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_CLOSE: {
        WindowEvent ev(WindowEvent::Type::Close);
        push_event(ev);
        should_close_ = true;
        return 0;
    }

    case WM_SIZE: {
        uint32_t w = LOWORD(lparam);
        uint32_t h = HIWORD(lparam);
        if (w != width_ || h != height_) {
            width_ = w;
            height_ = h;
            WindowEvent ev(WindowEvent::Type::Resize);
            ev.resize.width = w;
            ev.resize.height = h;
            push_event(ev);
        }
        return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        WindowEvent ev(WindowEvent::Type::KeyDown);
        ev.key.keycode = static_cast<uint32_t>(wparam);
        ev.key.repeat = (lparam & 0x40000000) != 0;
        push_event(ev);
        return 0;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP: {
        WindowEvent ev(WindowEvent::Type::KeyUp);
        ev.key.keycode = static_cast<uint32_t>(wparam);
        ev.key.repeat = false;
        push_event(ev);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int32_t x = static_cast<int16_t>(LOWORD(lparam));
        int32_t y = static_cast<int16_t>(HIWORD(lparam));

        WindowEvent ev(WindowEvent::Type::MouseMove);
        ev.mouse_move.x = x;
        ev.mouse_move.y = y;
        ev.mouse_move.dx = mouse_tracked_ ? (x - last_mouse_x_) : 0;
        ev.mouse_move.dy = mouse_tracked_ ? (y - last_mouse_y_) : 0;
        push_event(ev);

        last_mouse_x_ = x;
        last_mouse_y_ = y;
        mouse_tracked_ = true;
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        WindowEvent ev(WindowEvent::Type::MouseButtonDown);
        ev.mouse_button.button = (msg == WM_LBUTTONDOWN) ? 0 : (msg == WM_RBUTTONDOWN) ? 1 : 2;
        ev.mouse_button.x = static_cast<int16_t>(LOWORD(lparam));
        ev.mouse_button.y = static_cast<int16_t>(HIWORD(lparam));
        push_event(ev);
        return 0;
    }

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
        WindowEvent ev(WindowEvent::Type::MouseButtonUp);
        ev.mouse_button.button = (msg == WM_LBUTTONUP) ? 0 : (msg == WM_RBUTTONUP) ? 1 : 2;
        ev.mouse_button.x = static_cast<int16_t>(LOWORD(lparam));
        ev.mouse_button.y = static_cast<int16_t>(HIWORD(lparam));
        push_event(ev);
        return 0;
    }

    case WM_SETFOCUS: {
        WindowEvent ev(WindowEvent::Type::Focus);
        push_event(ev);
        return 0;
    }

    case WM_KILLFOCUS: {
        WindowEvent ev(WindowEvent::Type::Blur);
        push_event(ev);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

#endif // _WIN32
