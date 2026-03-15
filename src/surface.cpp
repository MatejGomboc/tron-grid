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
#include <Windows.h>
#endif
#include "surface.hpp"

vk::raii::SurfaceKHR createSurface(const vk::raii::Instance& instance, const WindowLib::Window& window)
{
#ifdef _WIN32
    vk::Win32SurfaceCreateInfoKHR create_info{};
    create_info.hinstance = static_cast<HINSTANCE>(window.nativeDisplay());
    create_info.hwnd = static_cast<HWND>(window.nativeHandle());

    return instance.createWin32SurfaceKHR(create_info);
#else
    vk::XcbSurfaceCreateInfoKHR create_info{};
    create_info.connection = static_cast<xcb_connection_t*>(window.nativeDisplay());
    create_info.window = static_cast<xcb_window_t>(reinterpret_cast<uintptr_t>(window.nativeHandle()));

    return instance.createXcbSurfaceKHR(create_info);
#endif
}

std::vector<const char*> requiredSurfaceExtensions()
{
    return {
        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef _WIN32
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#else
        VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#endif
    };
}
