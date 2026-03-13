/*
 * TronGrid — Vulkan physical + logical device
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <volk/volk.h>

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

#include <string>

namespace gpu
{

    class Instance; // forward declaration

    /// Selects the best physical device and creates a logical device with
    /// graphics + present queues.
    class Device {
    public:
        /// Pick the best GPU and create a logical device.
        /// The surface is needed to check present queue support.
        Device(const Instance& instance, VkSurfaceKHR surface);

        // Non-copyable, movable
        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;
        Device(Device&&) = default;
        Device& operator=(Device&&) = default;

        const vk::raii::Device& get() const
        {
            return device_;
        }

        const vk::raii::Queue& graphics_queue() const
        {
            return graphics_queue_;
        }

        const vk::raii::Queue& present_queue() const
        {
            return present_queue_;
        }

        uint32_t graphics_family_index() const
        {
            return graphics_family_index_;
        }

        uint32_t present_family_index() const
        {
            return present_family_index_;
        }

        const std::string& name() const
        {
            return device_name_;
        }

    private:
        vk::raii::PhysicalDevice physical_device_{nullptr};
        vk::raii::Device device_{nullptr};
        vk::raii::Queue graphics_queue_{nullptr};
        vk::raii::Queue present_queue_{nullptr};
        uint32_t graphics_family_index_ = 0;
        uint32_t present_family_index_ = 0;
        std::string device_name_;
    };

} // namespace gpu
