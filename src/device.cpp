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

#include "device.hpp"
#include "instance.hpp"
#include <algorithm>
#include <cstdlib>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <vector>

//! Required device extensions.
static constexpr const char* REQUIRED_DEVICE_EXTENSIONS[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_EXT_MESH_SHADER_EXTENSION_NAME,
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_KHR_RAY_QUERY_EXTENSION_NAME,
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
};

//! Holds graphics and present queue family indices discovered during device selection.
struct QueueFamilyIndices {
    uint32_t graphics{UINT32_MAX}; //!< Graphics queue family index.
    uint32_t present{UINT32_MAX}; //!< Present queue family index.

    //! Returns true if both graphics and present queue families have been found.
    [[nodiscard]] bool isComplete() const
    {
        return (graphics != UINT32_MAX) && (present != UINT32_MAX);
    }
};

//! Finds graphics and present queue family indices for the given device and surface.
static QueueFamilyIndices findQueueFamilies(const vk::raii::PhysicalDevice& device, VkSurfaceKHR surface)
{
    QueueFamilyIndices indices;
    std::vector<vk::QueueFamilyProperties> families{device.getQueueFamilyProperties()};

    for (uint32_t i = 0; i < static_cast<uint32_t>(families.size()); ++i) {
        // Graphics support
        if (families[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            indices.graphics = i;
        }

        // Present support
        vk::Bool32 present_support{device.getSurfaceSupportKHR(i, surface)};
        if (present_support) {
            indices.present = i;
        }

        // Prefer a single family that supports both (better performance)
        if (indices.graphics == indices.present && indices.isComplete()) {
            break;
        }
    }

    return indices;
}

//! Checks whether the device supports all required extensions.
static bool hasRequiredExtensions(const vk::raii::PhysicalDevice& device)
{
    std::vector<vk::ExtensionProperties> available{device.enumerateDeviceExtensionProperties()};

    for (const char* required : REQUIRED_DEVICE_EXTENSIONS) {
        bool found{std::ranges::any_of(available, [required](const vk::ExtensionProperties& ext) {
            return std::string_view(ext.extensionName.data()) == required;
        })};
        if (!found) {
            return false;
        }
    }

    return true;
}

//! Scores a physical device for suitability; returns -1 if unsuitable.
static int rateDevice(const vk::raii::PhysicalDevice& device, VkSurfaceKHR surface)
{
    vk::PhysicalDeviceProperties properties{device.getProperties()};
    QueueFamilyIndices indices{findQueueFamilies(device, surface)};

    // Must support Vulkan 1.3 (for synchronization2, dynamic rendering)
    if (properties.apiVersion < VK_API_VERSION_1_3) {
        return -1;
    }

    // Must have graphics + present queues and required extensions
    if (!indices.isComplete() || !hasRequiredExtensions(device)) {
        return -1;
    }

    int score{0};

    // Strongly prefer discrete GPUs
    if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
        score += 10000;
    } else if (properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu) {
        score += 1000;
    }

    // Bonus for single queue family (graphics + present on same family)
    if (indices.graphics == indices.present) {
        score += 100;
    }

    return score;
}

Device::Device(const Instance& instance, VkSurfaceKHR surface, LoggingLib::Logger& logger) :
    m_logger(logger)
{
    // Step 1: Enumerate physical devices
    std::vector<vk::raii::PhysicalDevice> physical_devices{instance.get().enumeratePhysicalDevices()};
    if (physical_devices.empty()) {
        m_logger.logFatal("No Vulkan-capable GPU found.");
        std::abort();
        return;
    }

    // Step 2: Score and pick the best device
    int best_score{-1};
    size_t best_index{0};

    for (size_t i = 0; i < physical_devices.size(); ++i) {
        int score{rateDevice(physical_devices[i], surface)};
        vk::PhysicalDeviceProperties props{physical_devices[i].getProperties()};
        m_logger.logInfo("GPU " + std::to_string(i) + ": " + props.deviceName.data() + " (score: " + std::to_string(score) + ").");

        if (score > best_score) {
            best_score = score;
            best_index = i;
        }
    }

    if (best_score < 0) {
        m_logger.logFatal("No suitable GPU found (need graphics + present queues and VK_KHR_swapchain).");
        std::abort();
        return;
    }

    m_physical_device = std::move(physical_devices[best_index]);
    vk::PhysicalDeviceProperties properties{m_physical_device.getProperties()};
    m_device_name = properties.deviceName.data();
    m_logger.logInfo("Selected GPU: " + m_device_name + ".");

    // Step 3: Find queue families
    QueueFamilyIndices indices{findQueueFamilies(m_physical_device, surface)};
    m_graphics_family_index = indices.graphics;
    m_present_family_index = indices.present;

    // Step 4: Create logical device
    constexpr float QUEUE_PRIORITY{1.0f};
    std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;

    // Deduplicate queue family indices
    std::set<uint32_t> unique_families = {m_graphics_family_index, m_present_family_index};
    for (uint32_t family : unique_families) {
        vk::DeviceQueueCreateInfo queue_info{};
        queue_info.queueFamilyIndex = family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &QUEUE_PRIORITY;
        queue_create_infos.push_back(queue_info);
    }

    // Enable synchronization2 (Vulkan 1.3 core)
    vk::PhysicalDeviceSynchronization2Features sync2_features{};
    sync2_features.synchronization2 = vk::True;

    // Enable dynamic rendering (Vulkan 1.3 core)
    vk::PhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features{};
    dynamic_rendering_features.dynamicRendering = vk::True;
    dynamic_rendering_features.pNext = &sync2_features;

    // Enable shader draw parameters (Vulkan 1.1 core) — needed for SV_InstanceID in SSBO indexing
    vk::PhysicalDeviceShaderDrawParametersFeatures shader_draw_params{};
    shader_draw_params.shaderDrawParameters = vk::True;
    shader_draw_params.pNext = &dynamic_rendering_features;

    // Enable mesh shader features (VK_EXT_mesh_shader)
    vk::PhysicalDeviceMeshShaderFeaturesEXT mesh_shader_features{};
    mesh_shader_features.meshShader = vk::True;
    mesh_shader_features.taskShader = vk::True;
    mesh_shader_features.pNext = &shader_draw_params;

    // Enable acceleration structure features (VK_KHR_acceleration_structure)
    vk::PhysicalDeviceAccelerationStructureFeaturesKHR accel_features{};
    accel_features.accelerationStructure = vk::True;
    accel_features.pNext = &mesh_shader_features;

    // Enable ray query features (VK_KHR_ray_query)
    vk::PhysicalDeviceRayQueryFeaturesKHR ray_query_features{};
    ray_query_features.rayQuery = vk::True;
    ray_query_features.pNext = &accel_features;

    // Enable Vulkan 1.2 features — all promoted features go here (not separate structs)
    vk::PhysicalDeviceVulkan12Features vulkan12_features{};
    vulkan12_features.drawIndirectCount = vk::True;
    vulkan12_features.descriptorBindingStorageBufferUpdateAfterBind = vk::True;
    vulkan12_features.descriptorBindingPartiallyBound = vk::True;
    vulkan12_features.runtimeDescriptorArray = vk::True;
    vulkan12_features.shaderInt8 = vk::True;
    vulkan12_features.uniformAndStorageBuffer8BitAccess = vk::True;
    vulkan12_features.storageBuffer8BitAccess = vk::True;
    vulkan12_features.bufferDeviceAddress = vk::True;
    vulkan12_features.pNext = &ray_query_features;

    vk::PhysicalDeviceFeatures2 features2{};
    features2.features.multiDrawIndirect = vk::True;
    features2.features.shaderStorageBufferArrayDynamicIndexing = vk::True;
    features2.pNext = &vulkan12_features;

    vk::DeviceCreateInfo device_info{};
    device_info.pNext = &features2;
    device_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
    device_info.pQueueCreateInfos = queue_create_infos.data();
    device_info.enabledExtensionCount = static_cast<uint32_t>(std::size(REQUIRED_DEVICE_EXTENSIONS));
    device_info.ppEnabledExtensionNames = REQUIRED_DEVICE_EXTENSIONS;

    m_device = vk::raii::Device(m_physical_device, device_info);

    // Step 5: Load device-level function pointers
    volkLoadDevice(*m_device);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*m_device);

    // Step 6: Retrieve queue handles
    m_graphics_queue = m_device.getQueue(m_graphics_family_index, 0);
    m_present_queue = m_device.getQueue(m_present_family_index, 0);

    // Step 7: Log acceleration structure properties
    vk::StructureChain<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceAccelerationStructurePropertiesKHR> props_chain{
        m_physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceAccelerationStructurePropertiesKHR>()};
    const vk::PhysicalDeviceAccelerationStructurePropertiesKHR& as_props{props_chain.get<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>()};
    m_logger.logInfo("RT: acceleration structure scratch alignment = " + std::to_string(as_props.minAccelerationStructureScratchOffsetAlignment)
        + ", max instances = " + std::to_string(as_props.maxInstanceCount) + ".");
}
