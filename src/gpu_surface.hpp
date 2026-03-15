/*
    TronGrid — Vulkan surface creation from native window handles
    Copyright (C) 2026 Matej Gomboc
    SPDX-Licence-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <volk/volk.h>
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <window/window.hpp>

namespace Gpu
{

    //! create a vk::raii::SurfaceKHR from the platform-native window handles.
    [[nodiscard]] vk::raii::SurfaceKHR createSurface(const vk::raii::Instance& instance, const Window& window);

    //! required surface extensions for the current platform.
    [[nodiscard]] std::vector<const char*> requiredSurfaceExtensions();

} // namespace Gpu
