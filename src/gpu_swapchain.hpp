/*
 * TronGrid — Vulkan swapchain + image views
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <volk/volk.h>

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

#include <vector>

namespace Gpu
{

    class Device; // forward declaration

    //! owns the Vulkan swapchain and its per-image views.
    //! supports recreation on window resize.
    class Swapchain {
    public:
        //! create a swapchain for the given device and surface.
        Swapchain(const Device& device, VkSurfaceKHR surface, uint32_t width, uint32_t height);

        // Non-copyable, movable
        Swapchain(const Swapchain&) = delete;
        Swapchain& operator=(const Swapchain&) = delete;
        Swapchain(Swapchain&&) = default;
        Swapchain& operator=(Swapchain&&) = default;

        //! recreate the swapchain (e.g., after window resize).
        void recreate(uint32_t width, uint32_t height);

        //! RAII swapchain reference.
        [[nodiscard]] const vk::raii::SwapchainKHR& get() const
        {
            return m_swapchain;
        }

        //! swapchain images (non-owning, managed by the swapchain).
        [[nodiscard]] const std::vector<vk::Image>& images() const
        {
            return m_images;
        }

        //! per-image views.
        [[nodiscard]] const std::vector<vk::raii::ImageView>& views() const
        {
            return m_views;
        }

        //! chosen surface format.
        [[nodiscard]] vk::SurfaceFormatKHR format() const
        {
            return m_format;
        }

        //! current swapchain extent.
        [[nodiscard]] vk::Extent2D extent() const
        {
            return m_extent;
        }

        //! number of swapchain images.
        [[nodiscard]] uint32_t imageCount() const
        {
            return static_cast<uint32_t>(m_images.size());
        }

    private:
        //! internal: query surface and build swapchain + views.
        void build(uint32_t width, uint32_t height);

        const Device* m_device = nullptr; //!< back-pointer to the device (non-owning)
        VkSurfaceKHR m_surface = VK_NULL_HANDLE; //!< surface handle (non-owning)
        vk::raii::SwapchainKHR m_swapchain{nullptr}; //!< swapchain handle
        std::vector<vk::Image> m_images; //!< swapchain images (non-owning)
        std::vector<vk::raii::ImageView> m_views; //!< per-image views
        vk::SurfaceFormatKHR m_format{}; //!< chosen surface format
        vk::PresentModeKHR m_present_mode{}; //!< chosen present mode
        vk::Extent2D m_extent{}; //!< current extent
    };

} // namespace Gpu
