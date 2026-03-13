/*
 * TronGrid — Vulkan instance + debug messenger
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#include "gpu_instance.hpp"

#include <algorithm>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

// Storage for the vulkan-hpp dynamic dispatcher (exactly one translation unit)
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* /*user_data*/)
{
    const char* prefix = "[Vulkan]";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        prefix = "[Vulkan ERROR]";
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        prefix = "[Vulkan WARNING]";
    }

    std::cerr << prefix << " " << callback_data->pMessage << "\n";
    return VK_FALSE;
}

namespace gpu
{

    static bool is_layer_available(const std::vector<vk::LayerProperties>& available, const char* name)
    {
        return std::ranges::any_of(available, [name](const vk::LayerProperties& layer) {
            return std::string_view(layer.layerName.data()) == name;
        });
    }

    static bool is_extension_available(const std::vector<vk::ExtensionProperties>& available, const char* name)
    {
        return std::ranges::any_of(available, [name](const vk::ExtensionProperties& ext) {
            return std::string_view(ext.extensionName.data()) == name;
        });
    }

    Instance::Instance(bool enable_validation, const std::vector<const char*>& required_surface_extensions)
    {
        // Step 1: Volk — find the Vulkan loader on this system
        if (volkInitialize() != VK_SUCCESS) {
            throw std::runtime_error("Vulkan not found on this system");
        }

        // Feed Volk's vkGetInstanceProcAddr to the vulkan-hpp dynamic dispatcher
        VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

        // Step 2: Check Vulkan version >= 1.3
        uint32_t api_version = vk::enumerateInstanceVersion();
        if (VK_API_VERSION_MAJOR(api_version) < 1 || (VK_API_VERSION_MAJOR(api_version) == 1 && VK_API_VERSION_MINOR(api_version) < 3)) {
            throw std::runtime_error("Vulkan 1.3 or later required (found " + std::to_string(VK_API_VERSION_MAJOR(api_version)) + "." +
                std::to_string(VK_API_VERSION_MINOR(api_version)) + ")");
        }

        // Step 3: Gather required instance extensions
        std::vector<const char*> extensions(required_surface_extensions.begin(), required_surface_extensions.end());

        if (enable_validation) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        // Verify all extensions are available
        std::vector<vk::ExtensionProperties> available_extensions = vk::enumerateInstanceExtensionProperties();
        for (const char* ext : extensions) {
            if (!is_extension_available(available_extensions, ext)) {
                throw std::runtime_error(std::string("Required Vulkan instance extension not available: ") + ext);
            }
        }

        // Step 4: Validation layers (debug only)
        std::vector<const char*> layers;
        if (enable_validation) {
            std::vector<vk::LayerProperties> available_layers = vk::enumerateInstanceLayerProperties();
            if (is_layer_available(available_layers, "VK_LAYER_KHRONOS_validation")) {
                layers.push_back("VK_LAYER_KHRONOS_validation");
            } else {
                std::cerr << "[TronGrid] Warning: VK_LAYER_KHRONOS_validation not available\n";
            }
        }

        // Step 5: Create instance
        vk::ApplicationInfo app_info{};
        app_info.pApplicationName = "TronGrid";
        app_info.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
        app_info.pEngineName = "TronGrid";
        app_info.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
        app_info.apiVersion = VK_API_VERSION_1_3;

        vk::InstanceCreateInfo create_info{};
        create_info.pApplicationInfo = &app_info;
        create_info.enabledLayerCount = static_cast<uint32_t>(layers.size());
        create_info.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
        create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        create_info.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();

        // Chain debug messenger to instance creation so we catch create/destroy messages
        vk::DebugUtilsMessengerCreateInfoEXT debug_create_info{};
        if (enable_validation && !layers.empty()) {
            debug_create_info.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
            debug_create_info.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
            debug_create_info.setPfnUserCallback(vulkan_debug_callback);
            create_info.pNext = &debug_create_info;
        }

        instance_ = vk::raii::Instance(context_, create_info);

        // Step 6: Load instance-level function pointers via Volk
        volkLoadInstanceOnly(*instance_);
        VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance_);

        // Step 7: Create persistent debug messenger
        if (enable_validation && !layers.empty()) {
            debug_messenger_ = vk::raii::DebugUtilsMessengerEXT(instance_, debug_create_info);
        }
    }

} // namespace gpu
