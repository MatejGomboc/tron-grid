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

    //! selects the best physical device and creates a logical device with
    //! graphics + present queues.
    class Device {
    public:
        //! pick the best GPU and create a logical device.
        //! the surface is needed to check present queue support.
        Device(const Instance& instance, VkSurfaceKHR surface);

        // Non-copyable, movable
        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;
        Device(Device&&) = default;
        Device& operator=(Device&&) = default;

        //! RAII device reference.
        [[nodiscard]] const vk::raii::Device& get() const
        {
            return device_;
        }

        //! graphics queue handle.
        [[nodiscard]] const vk::raii::Queue& graphics_queue() const
        {
            return graphics_queue_;
        }

        //! present queue handle.
        [[nodiscard]] const vk::raii::Queue& present_queue() const
        {
            return present_queue_;
        }

        //! graphics queue family index.
        [[nodiscard]] uint32_t graphics_family_index() const
        {
            return graphics_family_index_;
        }

        //! present queue family index.
        [[nodiscard]] uint32_t present_family_index() const
        {
            return present_family_index_;
        }

        //! human-readable GPU name.
        [[nodiscard]] const std::string& name() const
        {
            return device_name_;
        }

    private:
        vk::raii::PhysicalDevice physical_device_{nullptr}; //!< selected physical device
        vk::raii::Device device_{nullptr}; //!< logical device handle
        vk::raii::Queue graphics_queue_{nullptr}; //!< graphics queue
        vk::raii::Queue present_queue_{nullptr}; //!< present queue
        uint32_t graphics_family_index_ = 0; //!< graphics queue family index
        uint32_t present_family_index_ = 0; //!< present queue family index
        std::string device_name_; //!< human-readable GPU name
    };

} // namespace gpu
