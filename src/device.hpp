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
#include <string>

class Instance; // forward declaration

//! Selects the best physical device and creates a logical device with graphics + present queues.
class Device {
public:
    //! Pick the best GPU and create a logical device; the surface is needed to check present queue support.
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

    //! Graphics queue handle.
    [[nodiscard]] const vk::raii::Queue& graphicsQueue() const
    {
        return m_graphics_queue;
    }

    //! Present queue handle.
    [[nodiscard]] const vk::raii::Queue& presentQueue() const
    {
        return m_present_queue;
    }

    //! Graphics queue family index.
    [[nodiscard]] uint32_t graphicsFamilyIndex() const
    {
        return m_graphics_family_index;
    }

    //! Present queue family index.
    [[nodiscard]] uint32_t presentFamilyIndex() const
    {
        return m_present_family_index;
    }

    //! Human-readable GPU name.
    [[nodiscard]] const std::string& name() const
    {
        return m_device_name;
    }

private:
    vk::raii::PhysicalDevice m_physical_device{nullptr}; //!< Selected physical device.
    vk::raii::Device m_device{nullptr}; //!< Logical device handle.
    vk::raii::Queue m_graphics_queue{nullptr}; //!< Graphics queue.
    vk::raii::Queue m_present_queue{nullptr}; //!< Present queue.
    uint32_t m_graphics_family_index{0}; //!< Graphics queue family index.
    uint32_t m_present_family_index{0}; //!< Present queue family index.
    std::string m_device_name; //!< Human-readable GPU name.
};
