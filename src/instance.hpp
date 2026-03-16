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
#include <log/logger.hpp>
#include <vector>

//! Owns the Vulkan instance and (in debug) the validation debug messenger, destruction order is handled by vk::raii — no manual cleanup needed.
class Instance {
public:
    /*!
        Create a Vulkan instance with the given surface extensions.
        If enable_validation is true, enables VK_LAYER_KHRONOS_validation
        and VK_EXT_debug_utils with a stderr callback.
    */
    Instance(bool enable_validation, const std::vector<const char*>& required_surface_extensions, LoggingLib::Logger& logger);

    // Non-copyable, movable
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    Instance(Instance&&) = default;
    Instance& operator=(Instance&&) = default;

    //! Raw VkInstance handle (for surface creation, etc.).
    [[nodiscard]] VkInstance handle() const
    {
        return *m_instance;
    }

    //! RAII instance reference.
    [[nodiscard]] const vk::raii::Instance& get() const
    {
        return m_instance;
    }

private:
    LoggingLib::Logger& m_logger; //!< Logger reference (non-owning).
    vk::raii::Context m_context; //!< Vulkan context (loader bootstrap).
    vk::raii::Instance m_instance{nullptr}; //!< Vulkan instance handle.
    vk::raii::DebugUtilsMessengerEXT m_debug_messenger{nullptr}; //!< Validation debug messenger (debug only).
};
