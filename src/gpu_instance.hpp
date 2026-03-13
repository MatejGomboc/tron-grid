/*
 * TronGrid — Vulkan instance + debug messenger
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <volk/volk.h>

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

#include <vector>

namespace gpu
{

    //! owns the Vulkan instance and (in debug) the validation debug messenger.
    //! destruction order is handled by vk::raii — no manual cleanup needed.
    class Instance {
    public:
        //! create a Vulkan instance with the given surface extensions.
        //! if enable_validation is true, enables VK_LAYER_KHRONOS_validation
        //! and VK_EXT_debug_utils with a stderr callback.
        Instance(bool enable_validation, const std::vector<const char*>& required_surface_extensions);

        // Non-copyable, movable
        Instance(const Instance&) = delete;
        Instance& operator=(const Instance&) = delete;
        Instance(Instance&&) = default;
        Instance& operator=(Instance&&) = default;

        //! raw VkInstance handle (for surface creation, etc.)
        [[nodiscard]] VkInstance handle() const
        {
            return *instance_;
        }

        //! RAII instance reference.
        [[nodiscard]] const vk::raii::Instance& get() const
        {
            return instance_;
        }

    private:
        vk::raii::Context context_; //!< Vulkan context (loader bootstrap)
        vk::raii::Instance instance_{nullptr}; //!< Vulkan instance handle
        vk::raii::DebugUtilsMessengerEXT debug_messenger_{nullptr}; //!< validation debug messenger (debug only)
    };

} // namespace gpu
