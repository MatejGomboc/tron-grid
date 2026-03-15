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

#include <volk/volk.h>
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <window/window.hpp>

//! Create a vk::raii::SurfaceKHR from the platform-native window handles.
[[nodiscard]] vk::raii::SurfaceKHR createSurface(const vk::raii::Instance& instance, const WindowLib::Window& window);

//! Required surface extensions for the current platform.
[[nodiscard]] std::vector<const char*> requiredSurfaceExtensions();
