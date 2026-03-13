/*
 * TronGrid — Vulkan physical + logical device
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#include "gpu_device.hpp"
#include "gpu_instance.hpp"

#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace gpu
{

    /// Required device extensions.
    static constexpr const char* REQUIRED_DEVICE_EXTENSIONS[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    struct QueueFamilyIndices {
        uint32_t graphics = UINT32_MAX;
        uint32_t present = UINT32_MAX;

        bool is_complete() const
        {
            return graphics != UINT32_MAX && present != UINT32_MAX;
        }
    };

    static QueueFamilyIndices find_queue_families(const vk::raii::PhysicalDevice& device, VkSurfaceKHR surface)
    {
        QueueFamilyIndices indices;
        auto families = device.getQueueFamilyProperties();

        for (uint32_t i = 0; i < families.size(); ++i) {
            // Graphics support
            if (families[i].queueFlags & vk::QueueFlagBits::eGraphics) {
                indices.graphics = i;
            }

            // Present support
            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(*device, i, surface, &present_support);
            if (present_support) {
                indices.present = i;
            }

            // Prefer a single family that supports both (better performance)
            if (indices.graphics == indices.present && indices.is_complete()) {
                break;
            }
        }

        return indices;
    }

    static bool has_required_extensions(const vk::raii::PhysicalDevice& device)
    {
        auto available = device.enumerateDeviceExtensionProperties();

        for (const char* required : REQUIRED_DEVICE_EXTENSIONS) {
            bool found = std::any_of(available.begin(), available.end(), [required](const vk::ExtensionProperties& ext) {
                return std::string(ext.extensionName.data()) == required;
            });
            if (!found) {
                return false;
            }
        }

        return true;
    }

    static int rate_device(const vk::raii::PhysicalDevice& device, VkSurfaceKHR surface)
    {
        auto properties = device.getProperties();
        auto indices = find_queue_families(device, surface);

        // Must have graphics + present queues and required extensions
        if (!indices.is_complete() || !has_required_extensions(device)) {
            return -1;
        }

        int score = 0;

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

    Device::Device(const Instance& instance, VkSurfaceKHR surface)
    {
        // Step 1: Enumerate physical devices
        auto physical_devices = instance.get().enumeratePhysicalDevices();
        if (physical_devices.empty()) {
            throw std::runtime_error("No Vulkan-capable GPU found");
        }

        // Step 2: Score and pick the best device
        int best_score = -1;
        size_t best_index = 0;

        for (size_t i = 0; i < physical_devices.size(); ++i) {
            int score = rate_device(physical_devices[i], surface);
            auto props = physical_devices[i].getProperties();
            std::cerr << "[TronGrid] GPU " << i << ": " << props.deviceName.data() << " (score: " << score << ")\n";

            if (score > best_score) {
                best_score = score;
                best_index = i;
            }
        }

        if (best_score < 0) {
            throw std::runtime_error("No suitable GPU found (need graphics + present queues and VK_KHR_swapchain)");
        }

        physical_device_ = std::move(physical_devices[best_index]);
        auto properties = physical_device_.getProperties();
        device_name_ = properties.deviceName.data();
        std::cout << "Selected GPU: " << device_name_ << "\n";

        // Step 3: Find queue families
        auto indices = find_queue_families(physical_device_, surface);
        graphics_family_index_ = indices.graphics;
        present_family_index_ = indices.present;

        // Step 4: Create logical device
        float queue_priority = 1.0f;
        std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;

        // Deduplicate queue family indices
        std::set<uint32_t> unique_families = {graphics_family_index_, present_family_index_};
        for (uint32_t family : unique_families) {
            vk::DeviceQueueCreateInfo queue_info{};
            queue_info.queueFamilyIndex = family;
            queue_info.queueCount = 1;
            queue_info.pQueuePriorities = &queue_priority;
            queue_create_infos.push_back(queue_info);
        }

        // Enable dynamic rendering (Vulkan 1.3 core)
        vk::PhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features{};
        dynamic_rendering_features.dynamicRendering = VK_TRUE;

        vk::PhysicalDeviceFeatures2 features2{};
        features2.pNext = &dynamic_rendering_features;

        vk::DeviceCreateInfo device_info{};
        device_info.pNext = &features2;
        device_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
        device_info.pQueueCreateInfos = queue_create_infos.data();
        device_info.enabledExtensionCount = static_cast<uint32_t>(std::size(REQUIRED_DEVICE_EXTENSIONS));
        device_info.ppEnabledExtensionNames = REQUIRED_DEVICE_EXTENSIONS;

        device_ = vk::raii::Device(physical_device_, device_info);

        // Step 5: Load device-level function pointers
        volkLoadDevice(*device_);
        VULKAN_HPP_DEFAULT_DISPATCHER.init(*device_);

        // Step 6: Retrieve queue handles
        graphics_queue_ = device_.getQueue(graphics_family_index_, 0);
        present_queue_ = device_.getQueue(present_family_index_, 0);
    }

} // namespace gpu
