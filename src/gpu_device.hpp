/*
    TronGrid — Vulkan physical + logical device
    Copyright (C) 2026 Matej Gomboc
    SPDX-Licence-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <volk/volk.h>

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

#include <string>

namespace Gpu
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

        //! RAII physical device reference.
        [[nodiscard]] const vk::raii::PhysicalDevice& physicalDevice() const
        {
            return m_physical_device;
        }

        //! RAII device reference.
        [[nodiscard]] const vk::raii::Device& get() const
        {
            return m_device;
        }

        //! graphics queue handle.
        [[nodiscard]] const vk::raii::Queue& graphicsQueue() const
        {
            return m_graphics_queue;
        }

        //! present queue handle.
        [[nodiscard]] const vk::raii::Queue& presentQueue() const
        {
            return m_present_queue;
        }

        //! graphics queue family index.
        [[nodiscard]] uint32_t graphicsFamilyIndex() const
        {
            return m_graphics_family_index;
        }

        //! present queue family index.
        [[nodiscard]] uint32_t presentFamilyIndex() const
        {
            return m_present_family_index;
        }

        //! human-readable GPU name.
        [[nodiscard]] const std::string& name() const
        {
            return m_device_name;
        }

    private:
        vk::raii::PhysicalDevice m_physical_device{nullptr}; //!< selected physical device
        vk::raii::Device m_device{nullptr}; //!< logical device handle
        vk::raii::Queue m_graphics_queue{nullptr}; //!< graphics queue
        vk::raii::Queue m_present_queue{nullptr}; //!< present queue
        uint32_t m_graphics_family_index = 0; //!< graphics queue family index
        uint32_t m_present_family_index = 0; //!< present queue family index
        std::string m_device_name; //!< human-readable GPU name
    };

} // namespace Gpu
