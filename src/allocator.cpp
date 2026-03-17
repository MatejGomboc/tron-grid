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

#include "allocator.hpp"
#include "instance.hpp"
#include "device.hpp"
#include <cstdlib>

Allocator::Allocator(const Instance& instance, const Device& device, LoggingLib::Logger& logger) :
    m_logger(logger)
{
    VmaAllocatorCreateInfo alloc_info{};
    alloc_info.instance = instance.handle();
    alloc_info.physicalDevice = *device.physicalDevice();
    alloc_info.device = *device.get();
    alloc_info.vulkanApiVersion = VK_API_VERSION_1_3;

    VmaVulkanFunctions vma_functions{};
    VkResult result = vmaImportVulkanFunctionsFromVolk(&alloc_info, &vma_functions);
    if (result != VK_SUCCESS) {
        m_logger.logFatal("Failed to import Vulkan functions from Volk for VMA.");
        std::abort();
        return;
    }
    alloc_info.pVulkanFunctions = &vma_functions;

    result = vmaCreateAllocator(&alloc_info, &m_allocator);
    if (result != VK_SUCCESS) {
        m_logger.logFatal("Failed to create VMA allocator.");
        std::abort();
        return;
    }

    m_logger.logInfo("VMA allocator created.");
}

Allocator::~Allocator()
{
    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
        m_logger.logInfo("VMA allocator destroyed.");
    }
}

AllocatedBuffer Allocator::createBuffer(VkDeviceSize size, VkBufferUsageFlags buffer_usage, VmaAllocationCreateFlags alloc_flags, VmaMemoryUsage memory_usage) const
{
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = buffer_usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_create_info{};
    alloc_create_info.usage = memory_usage;
    alloc_create_info.flags = alloc_flags;

    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VkResult result = vmaCreateBuffer(m_allocator, &buffer_info, &alloc_create_info, &buffer, &allocation, nullptr);
    if (result != VK_SUCCESS) {
        m_logger.logFatal("Failed to create VMA buffer.");
        std::abort();
    }

    return AllocatedBuffer(m_allocator, buffer, allocation);
}
