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

class Device; // forward declaration

/*!
    Owns the Vulkan swapchain and its per-image views.
    Supports recreation on window resize.
*/
class Swapchain {
public:
    //! Create a swapchain for the given device and surface.
    Swapchain(const Device& device, VkSurfaceKHR surface, uint32_t width, uint32_t height);

    // Non-copyable, movable
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = default;
    Swapchain& operator=(Swapchain&&) = default;

    //! Recreate the swapchain (e.g., after window resize).
    void recreate(uint32_t width, uint32_t height);

    //! RAII swapchain reference.
    [[nodiscard]] const vk::raii::SwapchainKHR& get() const
    {
        return m_swapchain;
    }

    //! Swapchain images (non-owning, managed by the swapchain).
    [[nodiscard]] const std::vector<vk::Image>& images() const
    {
        return m_images;
    }

    //! Per-image views.
    [[nodiscard]] const std::vector<vk::raii::ImageView>& views() const
    {
        return m_views;
    }

    //! Chosen surface format.
    [[nodiscard]] vk::SurfaceFormatKHR format() const
    {
        return m_format;
    }

    //! Current swapchain extent.
    [[nodiscard]] vk::Extent2D extent() const
    {
        return m_extent;
    }

    //! Number of swapchain images.
    [[nodiscard]] uint32_t imageCount() const
    {
        return static_cast<uint32_t>(m_images.size());
    }

private:
    //! Internal: query surface and build swapchain + views.
    void build(uint32_t width, uint32_t height);

    const Device* m_device = nullptr; //!< Back-pointer to the device (non-owning).
    VkSurfaceKHR m_surface = VK_NULL_HANDLE; //!< Surface handle (non-owning).
    vk::raii::SwapchainKHR m_swapchain{nullptr}; //!< Swapchain handle.
    std::vector<vk::Image> m_images; //!< Swapchain images (non-owning).
    std::vector<vk::raii::ImageView> m_views; //!< Per-image views.
    vk::SurfaceFormatKHR m_format{}; //!< Chosen surface format.
    vk::PresentModeKHR m_present_mode{}; //!< Chosen present mode.
    vk::Extent2D m_extent{}; //!< Current extent.
};
