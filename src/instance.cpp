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

#include "instance.hpp"
#include <algorithm>
#include <cstdlib>
#include <ranges>
#include <string>
#include <string_view>

// Storage for the vulkan-hpp dynamic dispatcher (exactly one translation unit)
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

//! Vulkan validation debug callback; routes messages through the Logger via pUserData.
static VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data)
{
    LoggingLib::Logger* logger = static_cast<LoggingLib::Logger*>(user_data);
    if (!logger) {
        return VK_FALSE;
    }

    std::string message = std::string("[Vulkan] ") + callback_data->pMessage;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        logger->logError(message);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        logger->logWarning(message);
    } else {
        logger->logInfo(message);
    }
    return VK_FALSE;
}

//! Checks whether a named layer exists in the available list.
static bool isLayerAvailable(const std::vector<vk::LayerProperties>& available, const char* name)
{
    return std::ranges::any_of(available, [name](const vk::LayerProperties& layer) {
        return std::string_view(layer.layerName.data()) == name;
    });
}

//! Checks whether a named extension exists in the available list.
static bool isExtensionAvailable(const std::vector<vk::ExtensionProperties>& available, const char* name)
{
    return std::ranges::any_of(available, [name](const vk::ExtensionProperties& ext) {
        return std::string_view(ext.extensionName.data()) == name;
    });
}

Instance::Instance(bool enable_validation, const std::vector<const char*>& required_surface_extensions, LoggingLib::Logger& logger) :
    m_logger(logger)
{
    // Step 1: Volk — find the Vulkan loader on this system
    if (volkInitialize() != VK_SUCCESS) {
        m_logger.logFatal("Vulkan not found on this system.");
        std::abort();
        return;
    }

    // Feed Volk's vkGetInstanceProcAddr to the vulkan-hpp dynamic dispatcher
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

    // Step 2: Check Vulkan version >= 1.3
    uint32_t api_version = vk::enumerateInstanceVersion();
    if (VK_API_VERSION_MAJOR(api_version) < 1 || (VK_API_VERSION_MAJOR(api_version) == 1 && VK_API_VERSION_MINOR(api_version) < 3)) {
        m_logger.logFatal("Vulkan 1.3 or later required (found " + std::to_string(VK_API_VERSION_MAJOR(api_version)) + "."
            + std::to_string(VK_API_VERSION_MINOR(api_version)) + ").");
        std::abort();
        return;
    }

    // Step 3: Gather required instance extensions
    std::vector<const char*> extensions(required_surface_extensions.begin(), required_surface_extensions.end());

    if (enable_validation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // Verify all extensions are available
    std::vector<vk::ExtensionProperties> available_extensions = vk::enumerateInstanceExtensionProperties();
    for (const char* ext : extensions) {
        if (!isExtensionAvailable(available_extensions, ext)) {
            m_logger.logFatal(std::string("Required Vulkan instance extension not available: ") + ext + ".");
            std::abort();
            return;
        }
    }

    // Step 4: Validation layers (debug only)
    std::vector<const char*> layers;
    if (enable_validation) {
        std::vector<vk::LayerProperties> available_layers = vk::enumerateInstanceLayerProperties();
        if (isLayerAvailable(available_layers, "VK_LAYER_KHRONOS_validation")) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
        } else {
            m_logger.logWarning("VK_LAYER_KHRONOS_validation not available.");
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
        debug_create_info.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
            | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
        debug_create_info.setPfnUserCallback(vulkanDebugCallback);
        debug_create_info.pUserData = &m_logger;
        create_info.pNext = &debug_create_info;
    }

    m_instance = vk::raii::Instance(m_context, create_info);

    // Step 6: Load instance-level function pointers via Volk
    volkLoadInstanceOnly(*m_instance);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*m_instance);

    // Step 7: Create persistent debug messenger
    if (enable_validation && !layers.empty()) {
        m_debug_messenger = vk::raii::DebugUtilsMessengerEXT(m_instance, debug_create_info);
    }
}
