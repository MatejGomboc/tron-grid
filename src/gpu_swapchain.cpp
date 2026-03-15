/*
 * TronGrid — Vulkan swapchain + image views
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#include "gpu_swapchain.hpp"
#include "gpu_device.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <ranges>
#include <stdexcept>

namespace gpu
{

    static vk::SurfaceFormatKHR choose_surface_format(const std::vector<vk::SurfaceFormatKHR>& available)
    {
        // Prefer sRGB with B8G8R8A8 layout
        for (const vk::SurfaceFormatKHR& fmt : available) {
            if (fmt.format == vk::Format::eB8G8R8A8Srgb && fmt.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
                return fmt;
            }
        }

        // Fall back to first available
        return available.front();
    }

    static vk::PresentModeKHR choose_present_mode(const std::vector<vk::PresentModeKHR>& available)
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

    static vk::Extent2D choose_extent(const vk::SurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height)
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

    Swapchain::Swapchain(const Device& device, VkSurfaceKHR surface, uint32_t width, uint32_t height) : device_(&device), surface_(surface)
    {
        build(width, height);
    }

    void Swapchain::recreate(uint32_t width, uint32_t height)
    {
        // Wait for the device to finish before destroying old resources
        device_->get().waitIdle();

        // Clear old views first (they reference old images)
        views_.clear();
        images_.clear();

        // Rebuild with old swapchain for smoother transition
        build(width, height);
    }

    void Swapchain::build(uint32_t width, uint32_t height)
    {
        const vk::raii::PhysicalDevice& physical_device = device_->physical_device();

        // Query surface capabilities
        vk::SurfaceCapabilitiesKHR capabilities = physical_device.getSurfaceCapabilitiesKHR(surface_);
        std::vector<vk::SurfaceFormatKHR> formats = physical_device.getSurfaceFormatsKHR(surface_);
        std::vector<vk::PresentModeKHR> present_modes = physical_device.getSurfacePresentModesKHR(surface_);

        if (formats.empty()) {
            throw std::runtime_error("No surface formats available");
        }

        // Choose optimal settings
        format_ = choose_surface_format(formats);
        present_mode_ = choose_present_mode(present_modes);
        extent_ = choose_extent(capabilities, width, height);

        // Image count: min + 1 for triple buffering headroom
        uint32_t image_count = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
            image_count = capabilities.maxImageCount;
        }

        // Create swapchain
        vk::SwapchainCreateInfoKHR create_info{};
        create_info.surface = surface_;
        create_info.minImageCount = image_count;
        create_info.imageFormat = format_.format;
        create_info.imageColorSpace = format_.colorSpace;
        create_info.imageExtent = extent_;
        create_info.imageArrayLayers = 1;
        create_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

        uint32_t graphics_family = device_->graphics_family_index();
        uint32_t present_family = device_->present_family_index();

        if (graphics_family != present_family) {
            uint32_t family_indices[] = {graphics_family, present_family};
            create_info.imageSharingMode = vk::SharingMode::eConcurrent;
            create_info.queueFamilyIndexCount = 2;
            create_info.pQueueFamilyIndices = family_indices;
        } else {
            create_info.imageSharingMode = vk::SharingMode::eExclusive;
        }

        create_info.preTransform = capabilities.currentTransform;
        create_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        create_info.presentMode = present_mode_;
        create_info.clipped = VK_TRUE;

        // Pass old swapchain for smoother recreation
        create_info.oldSwapchain = *swapchain_;

        swapchain_ = vk::raii::SwapchainKHR(device_->get(), create_info);

        // Retrieve swapchain images
        images_ = swapchain_.getImages();

        std::cout << "Swapchain created: " << extent_.width << "x" << extent_.height << " (" << images_.size() << " images, "
                  << (present_mode_ == vk::PresentModeKHR::eMailbox ? "MAILBOX" : "FIFO") << ")\n";

        // Create image views
        views_.clear();
        views_.reserve(images_.size());

        for (vk::Image image : images_) {
            vk::ImageViewCreateInfo view_info{};
            view_info.image = image;
            view_info.viewType = vk::ImageViewType::e2D;
            view_info.format = format_.format;
            view_info.components.r = vk::ComponentSwizzle::eIdentity;
            view_info.components.g = vk::ComponentSwizzle::eIdentity;
            view_info.components.b = vk::ComponentSwizzle::eIdentity;
            view_info.components.a = vk::ComponentSwizzle::eIdentity;
            view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;

            views_.push_back(vk::raii::ImageView(device_->get(), view_info));
        }
    }

} // namespace gpu
