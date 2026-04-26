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

#ifdef _WIN32

#include "win32_window.hpp"
#include <cstdlib>

namespace WindowLib
{

    bool Win32Window::m_class_registered{false};

    Win32Window::Win32Window(const WindowConfig& config, LoggingLib::Logger& logger) :
        Window(logger)
    {
        m_hinstance = GetModuleHandle(nullptr);

        // Register window class once. CS_OWNDC was previously included but is meaningless
        // here (we render via Vulkan, not GDI — there is no device context to "own"); dropped
        // for hygiene. CS_HREDRAW | CS_VREDRAW invalidate the client area on resize so DWM
        // immediately requests a repaint via WM_PAINT, eliminating stale-content artefacts.
        // hbrBackground = BLACK_BRUSH gives DWM a defined fill colour for any client-area
        // pixels that have not yet been written by the Vulkan swapchain (e.g. between window
        // creation and the first present), avoiding the "hollow" desktop-show-through window.
        if (!m_class_registered) {
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = wndProcStatic;
            wc.hInstance = m_hinstance;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
            wc.lpszClassName = CLASS_NAME;

            if (!RegisterClassExW(&wc)) {
                m_logger.logFatal("Failed to register Win32 window class.");
                std::abort();
                return;
            }
            m_class_registered = true;
        }

        // Window style
        DWORD style{WS_OVERLAPPEDWINDOW};
        if (!config.resizable) {
            style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        }
        if (!config.decorated) {
            style = WS_POPUP;
        }

        // Adjust window rect to account for borders/title bar
        RECT rect = {0, 0, static_cast<LONG>(config.width), static_cast<LONG>(config.height)};
        AdjustWindowRect(&rect, style, FALSE);

        int window_width{rect.right - rect.left};
        int window_height{rect.bottom - rect.top};

        // Centre on primary monitor
        int screen_width{GetSystemMetrics(SM_CXSCREEN)};
        int screen_height{GetSystemMetrics(SM_CYSCREEN)};
        int x{(screen_width - window_width) / 2};
        int y{(screen_height - window_height) / 2};

        // Convert title to wide string. MultiByteToWideChar with cchSrc = -1 returns the
        // count INCLUDING the trailing null terminator; sizing the wstring with that count
        // would leave a spurious L'\0' at back(). Strip the terminator from the size — the
        // wstring's own null terminator covers the C-string contract for CreateWindowExW.
        int title_len{MultiByteToWideChar(CP_UTF8, 0, config.title.c_str(), -1, nullptr, 0)};
        std::wstring title_wide(static_cast<std::wstring::size_type>(title_len > 0 ? title_len - 1 : 0), 0);
        MultiByteToWideChar(CP_UTF8, 0, config.title.c_str(), -1, title_wide.data(), title_len);

        // Set dimensions before CreateWindowExW — WM_SIZE is dispatched during creation
        // and wndProc compares against these to detect real resizes
        m_width = config.width;
        m_height = config.height;

        // No WS_EX_NOREDIRECTIONBITMAP — that flag excludes the window from DWM
        // compositing, which means the GDI hbrBackground (BLACK_BRUSH set on the window
        // class) is never painted between window creation and the first Vulkan present.
        // Result was a "hollow" window showing whatever was on the desktop behind it for a
        // visible moment. With DWM compositing enabled, BLACK_BRUSH paints immediately and
        // the window appears as a solid black rectangle until Vulkan takes over. The
        // tradeoff is that DWM's redirection bitmap may briefly stretch stale swapchain
        // content during a resize — far less jarring than the hollow startup window, and
        // our explicit Vulkan resize handling minimises that window of staleness anyway.
        m_hwnd = CreateWindowExW(0, CLASS_NAME, title_wide.c_str(), style, x, y, window_width, window_height, nullptr, nullptr, m_hinstance,
            this // Pass this pointer for WM_NCCREATE
        );

        if (!m_hwnd) {
            m_logger.logFatal("Failed to create Win32 window.");
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

    void Win32Window::waitEvents()
    {
        WaitMessage();
        pumpEvents();
    }

    void Win32Window::setCursorCaptured(bool captured)
    {
        // Early return if state is unchanged. ShowCursor maintains a per-process display
        // counter (the cursor is hidden iff counter < 0); calling ShowCursor(FALSE) twice
        // sinks the counter to -2 and a single ShowCursor(TRUE) only restores it to -1,
        // leaving the cursor invisible. Same balancing concern for SetCapture /
        // ReleaseCapture and ClipCursor. Idempotent calls must be no-ops.
        if (m_cursor_captured == captured) {
            return;
        }
        m_cursor_captured = captured;
        if (captured) {
            SetCapture(m_hwnd);
            ShowCursor(FALSE);
            // Clip cursor to client area
            RECT rect{};
            GetClientRect(m_hwnd, &rect);
            MapWindowPoints(m_hwnd, nullptr, reinterpret_cast<POINT*>(&rect), 2);
            ClipCursor(&rect);
            // Centre cursor so first delta is zero. The SetCursorPos triggers a synthetic
            // WM_MOUSEMOVE — flag it so the WM_MOUSEMOVE handler consumes that event
            // without emitting a duplicate.
            POINT centre{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
            SetCursorPos(centre.x, centre.y);
            m_last_mouse_x = static_cast<int32_t>((rect.right - rect.left) / 2);
            m_last_mouse_y = static_cast<int32_t>((rect.bottom - rect.top) / 2);
            m_warp_pending = true;
        } else {
            ClipCursor(nullptr);
            ShowCursor(TRUE);
            ReleaseCapture();
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
        Win32Window* self{nullptr};

        if (msg == WM_NCCREATE) {
            // Extract and store the this pointer
            auto* create_struct{reinterpret_cast<CREATESTRUCTW*>(lparam)};
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
            WindowEvent ev{WindowEvent::Type::Close};
            pushEvent(ev);
            m_should_close = true;
            return 0;
        }

        case WM_ERASEBKGND: {
            // Return non-zero to prevent Windows from erasing the client area to black.
            // The old Vulkan frame stays visible until the render thread presents the next one.
            return 1;
        }

        case WM_PAINT: {
            // Validate the window region so WM_PAINT stops firing, then push an Expose event.
            ValidateRect(hwnd, nullptr);
            WindowEvent ev{WindowEvent::Type::Expose};
            pushEvent(ev);
            return 0;
        }

        case WM_SIZE: {
            uint32_t w{LOWORD(lparam)};
            uint32_t h{HIWORD(lparam)};
            if (w != m_width || h != m_height) {
                m_width = w;
                m_height = h;
                WindowEvent ev{WindowEvent::Type::Resize};
                ev.resize.width = w;
                ev.resize.height = h;
                pushEvent(ev);
            }
            return 0;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            WindowEvent ev{WindowEvent::Type::KeyDown};
            ev.key.keycode = static_cast<uint32_t>(wparam);
            ev.key.repeat = (lparam & 0x40000000) != 0;
            pushEvent(ev);
            // For WM_SYSKEY* we must NOT return 0 — DefWindowProcW translates these into
            // WM_SYSCOMMAND for system-key actions like Alt+F4 (close window), Alt+Space
            // (system menu), and F10 (menu activation). Returning 0 silently breaks those.
            // For WM_KEYDOWN, falling through to DefWindowProcW is also safe (it's a no-op
            // for plain keys), so a single break covers both cases.
            break;
        }

        case WM_KEYUP:
        case WM_SYSKEYUP: {
            WindowEvent ev{WindowEvent::Type::KeyUp};
            ev.key.keycode = static_cast<uint32_t>(wparam);
            ev.key.repeat = false;
            pushEvent(ev);
            // Same WM_SYSKEY → DefWindowProcW concern as KeyDown above.
            break;
        }

        case WM_MOUSEMOVE: {
            int32_t x{static_cast<int16_t>(LOWORD(lparam))};
            int32_t y{static_cast<int16_t>(HIWORD(lparam))};

            // Filter the synthetic recentre event. After SetCursorPos in the previous
            // WM_MOUSEMOVE branch (or in setCursorCaptured), the OS dispatches a follow-up
            // WM_MOUSEMOVE with the new position. Without this filter, every real mouse
            // move emits two events — one real, one phantom (dx=0, dy=0) — doubling
            // consumer event load and potentially confusing camera-look code that integrates
            // dx/dy.
            if (m_warp_pending) {
                m_warp_pending = false;
                m_last_mouse_x = x;
                m_last_mouse_y = y;
                m_mouse_tracked = true;
                return 0;
            }

            int32_t dx{m_mouse_tracked ? (x - m_last_mouse_x) : 0};
            int32_t dy{m_mouse_tracked ? (y - m_last_mouse_y) : 0};

            if (m_cursor_captured && (dx != 0 || dy != 0)) {
                // Recentre cursor to prevent hitting screen edges
                RECT rect{};
                GetClientRect(hwnd, &rect);
                POINT centre{(rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2};
                m_last_mouse_x = centre.x;
                m_last_mouse_y = centre.y;
                POINT screen_centre{centre};
                ClientToScreen(hwnd, &screen_centre);
                SetCursorPos(screen_centre.x, screen_centre.y);
                m_warp_pending = true; // The next WM_MOUSEMOVE will be the synthetic recentre.
            } else {
                m_last_mouse_x = x;
                m_last_mouse_y = y;
            }

            WindowEvent ev{WindowEvent::Type::MouseMove};
            ev.mouse_move.x = x;
            ev.mouse_move.y = y;
            ev.mouse_move.dx = dx;
            ev.mouse_move.dy = dy;
            pushEvent(ev);

            m_mouse_tracked = true;
            return 0;
        }

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            WindowEvent ev{WindowEvent::Type::MouseButtonDown};
            ev.mouse_button.button = (msg == WM_LBUTTONDOWN) ? 0 : (msg == WM_RBUTTONDOWN) ? 1 : 2;
            ev.mouse_button.x = static_cast<int16_t>(LOWORD(lparam));
            ev.mouse_button.y = static_cast<int16_t>(HIWORD(lparam));
            pushEvent(ev);
            return 0;
        }

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            WindowEvent ev{WindowEvent::Type::MouseButtonUp};
            ev.mouse_button.button = (msg == WM_LBUTTONUP) ? 0 : (msg == WM_RBUTTONUP) ? 1 : 2;
            ev.mouse_button.x = static_cast<int16_t>(LOWORD(lparam));
            ev.mouse_button.y = static_cast<int16_t>(HIWORD(lparam));
            pushEvent(ev);
            return 0;
        }

        case WM_SETFOCUS: {
            WindowEvent ev{WindowEvent::Type::Focus};
            pushEvent(ev);
            return 0;
        }

        case WM_KILLFOCUS: {
            WindowEvent ev{WindowEvent::Type::Blur};
            pushEvent(ev);
            return 0;
        }
        }

        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

} // namespace WindowLib

#endif // _WIN32
