/*
 * TronGrid — Vulkan surface creation from native window handles
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <volk/volk.h>

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

#include <vector>
#include <window/window.hpp>

namespace gpu
{

    /// Create a vk::raii::SurfaceKHR from the platform-native window handles.
    vk::raii::SurfaceKHR create_surface(const vk::raii::Instance& instance, const Window& window);

    /// Required surface extensions for the current platform.
    std::vector<const char*> required_surface_extensions();

} // namespace gpu
