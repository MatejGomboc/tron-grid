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

    //! Win32 platform window implementation.
    class Win32Window : public Window {
    public:
        explicit Win32Window(const WindowConfig& config);
        ~Win32Window() override;

        //! Processes pending Win32 messages into the event queue.
        void pumpEvents() override;

        //! Returns the HWND as a void pointer.
        [[nodiscard]] void* nativeHandle() const override;

        //! Returns the HINSTANCE as a void pointer.
        [[nodiscard]] void* nativeDisplay() const override;

    private:
        //! Static window procedure thunk that forwards to the instance method.
        static LRESULT CALLBACK wndProcStatic(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

        //! Instance window procedure handling Win32 messages.
        LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

        HWND m_hwnd = nullptr; //!< Native window handle.
        HINSTANCE m_hinstance = nullptr; //!< Application instance handle.

        int32_t m_last_mouse_x = 0; //!< Last known mouse x for delta computation.
        int32_t m_last_mouse_y = 0; //!< Last known mouse y for delta computation.
        bool m_mouse_tracked = false; //!< True after the first mouse event has been received.

        static constexpr const wchar_t* CLASS_NAME = L"TronGridWindowClass"; //!< Win32 window class name.
        static bool m_class_registered; //!< Tracks whether the Win32 window class has been registered.
    };

} // namespace WindowLib

#endif // _WIN32
