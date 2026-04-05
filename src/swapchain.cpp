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

#include "swapchain.hpp"
#include "device.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <limits>
#include <ranges>
#include <string>

//! Selects the best surface format, preferring B8G8R8A8 UNORM for compute storage writes.
[[nodiscard]] static vk::SurfaceFormatKHR chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& available)
{
    assert(!available.empty());

    // Prefer UNORM with sRGB colour space — the post-process compute shader applies
    // sRGB gamma manually, so the format must support VK_IMAGE_USAGE_STORAGE_BIT
    // (sRGB formats do not). The display still interprets values as sRGB.
    std::vector<vk::SurfaceFormatKHR>::const_iterator it{std::ranges::find_if(available, [](const vk::SurfaceFormatKHR& fmt) {
        return fmt.format == vk::Format::eB8G8R8A8Unorm && fmt.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    })};

    if (it != available.end()) {
        return *it;
    }

    // Fall back to first available
    return available.front();
}

//! Selects the best present mode, preferring MAILBOX for low latency.
[[nodiscard]] static vk::PresentModeKHR choosePresentMode(const std::vector<vk::PresentModeKHR>& available)
{
    // Prefer MAILBOX (low latency, no tearing)
    if (std::ranges::any_of(available, [](vk::PresentModeKHR mode) {
            return mode == vk::PresentModeKHR::eMailbox;
        })) {
        return vk::PresentModeKHR::eMailbox;
    }

    // FIFO is always available (guaranteed by spec)
    return vk::PresentModeKHR::eFifo;
}

//! Clamps the requested dimensions to the surface capability range.
[[nodiscard]] static vk::Extent2D chooseExtent(const vk::SurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height)
{
    // If currentExtent is not the special 0xFFFFFFFF value, the surface size is fixed
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    // Otherwise, clamp the window dimensions to the surface's allowed range
    vk::Extent2D extent{};
    extent.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return extent;
}

Swapchain::Swapchain(const Device& device, VkSurfaceKHR surface, uint32_t width, uint32_t height, LoggingLib::Logger& logger) :
    m_logger(&logger),
    m_device(&device),
    m_surface(surface)
{
    build(width, height);
}

void Swapchain::recreate(uint32_t width, uint32_t height)
{
    // Wait for all GPU work (including present operations) to complete before
    // destroying the old swapchain and its semaphores. Fences alone are not
    // sufficient — they only track command buffer completion, not present.
    m_device->get().waitIdle();

    // Clear old views first (they reference old images)
    m_views.clear();
    m_images.clear();

    // Rebuild with old swapchain for smoother transition
    build(width, height);
}

void Swapchain::build(uint32_t width, uint32_t height)
{
    // Guard against zero-extent (minimised window) — Vulkan requires imageExtent > 0.
    if ((width == 0) || (height == 0)) {
        return;
    }

    const vk::raii::PhysicalDevice& physical_device{m_device->physicalDevice()};

    // Query surface capabilities
    vk::SurfaceCapabilitiesKHR capabilities{physical_device.getSurfaceCapabilitiesKHR(m_surface)};
    std::vector<vk::SurfaceFormatKHR> formats{physical_device.getSurfaceFormatsKHR(m_surface)};
    std::vector<vk::PresentModeKHR> present_modes{physical_device.getSurfacePresentModesKHR(m_surface)};

    if (formats.empty()) {
        m_logger->logFatal("No surface formats available.");
        std::abort();
        return;
    }

    // Choose optimal settings
    m_format = chooseSurfaceFormat(formats);
    m_present_mode = choosePresentMode(present_modes);
    m_extent = chooseExtent(capabilities, width, height);

    // Image count: min + 1 for triple buffering headroom
    uint32_t image_count{capabilities.minImageCount + 1};
    if ((capabilities.maxImageCount > 0) && (image_count > capabilities.maxImageCount)) {
        image_count = capabilities.maxImageCount;
    }

    // The post-process compute shader writes directly to swapchain images.
    if (!(capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eStorage)) {
        m_logger->logFatal("Swapchain does not support storage image usage.");
        std::abort();
        return;
    }

    // Verify the chosen format supports storage image writes (sRGB formats typically do not).
    vk::FormatProperties format_props{physical_device.getFormatProperties(m_format.format)};
    if (!(format_props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eStorageImage)) {
        m_logger->logFatal("Swapchain format does not support storage image writes.");
        std::abort();
        return;
    }

    // Create swapchain
    vk::SwapchainCreateInfoKHR create_info{};
    create_info.surface = m_surface;
    create_info.minImageCount = image_count;
    create_info.imageFormat = m_format.format;
    create_info.imageColorSpace = m_format.colorSpace;
    create_info.imageExtent = m_extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eStorage;

    uint32_t graphics_family{m_device->graphicsFamilyIndex()};
    uint32_t present_family{m_device->presentFamilyIndex()};
    std::array<uint32_t, 2> family_indices{graphics_family, present_family};

    if (graphics_family != present_family) {
        create_info.imageSharingMode = vk::SharingMode::eConcurrent;
        create_info.setQueueFamilyIndices(family_indices);
    } else {
        create_info.imageSharingMode = vk::SharingMode::eExclusive;
    }

    create_info.preTransform = capabilities.currentTransform;
    create_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    create_info.presentMode = m_present_mode;
    create_info.clipped = vk::True;

    // Pass old swapchain for smoother recreation
    create_info.oldSwapchain = *m_swapchain;

    m_swapchain = vk::raii::SwapchainKHR(m_device->get(), create_info);

    // Retrieve swapchain images
    m_images = m_swapchain.getImages();

    m_logger->logInfo("Swapchain created: " + std::to_string(m_extent.width) + "x" + std::to_string(m_extent.height) + " (" + std::to_string(m_images.size())
        + " images, " + (m_present_mode == vk::PresentModeKHR::eMailbox ? "MAILBOX" : "FIFO") + ").");

    // Create image views
    m_views.clear();
    m_views.reserve(m_images.size());

    for (vk::Image image : m_images) {
        vk::ImageViewCreateInfo view_info{};
        view_info.image = image;
        view_info.viewType = vk::ImageViewType::e2D;
        view_info.format = m_format.format;
        view_info.components.r = vk::ComponentSwizzle::eIdentity;
        view_info.components.g = vk::ComponentSwizzle::eIdentity;
        view_info.components.b = vk::ComponentSwizzle::eIdentity;
        view_info.components.a = vk::ComponentSwizzle::eIdentity;
        view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        m_views.push_back(vk::raii::ImageView(m_device->get(), view_info));
    }
}
