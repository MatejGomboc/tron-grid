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

#ifdef _WIN32
#include <Volk/volk.h>
#else
#include <volk/volk.h>
#endif

// Suppress warnings from third-party VMA header.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include <vma/vk_mem_alloc.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include <log/logger.hpp>

class Instance; // forward declaration
class Device; // forward declaration

//! RAII wrapper for a VMA buffer and its allocation.
class AllocatedBuffer {
public:
    //! Takes ownership of a VMA buffer+allocation pair.
    AllocatedBuffer(VmaAllocator allocator, VkBuffer buffer, VmaAllocation allocation) :
        m_allocator(allocator),
        m_buffer(buffer),
        m_allocation(allocation)
    {
    }

    ~AllocatedBuffer()
    {
        if (m_buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
        }
    }

    // Non-copyable
    AllocatedBuffer(const AllocatedBuffer&) = delete;
    AllocatedBuffer& operator=(const AllocatedBuffer&) = delete;

    // Movable
    AllocatedBuffer(AllocatedBuffer&& other) noexcept :
        m_allocator(other.m_allocator),
        m_buffer(other.m_buffer),
        m_allocation(other.m_allocation)
    {
        other.m_allocator = VK_NULL_HANDLE;
        other.m_buffer = VK_NULL_HANDLE;
        other.m_allocation = VK_NULL_HANDLE;
    }

    AllocatedBuffer& operator=(AllocatedBuffer&& other) noexcept
    {
        if (this != &other) {
            if (m_buffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
            }
            m_allocator = other.m_allocator;
            m_buffer = other.m_buffer;
            m_allocation = other.m_allocation;
            other.m_allocator = VK_NULL_HANDLE;
            other.m_buffer = VK_NULL_HANDLE;
            other.m_allocation = VK_NULL_HANDLE;
        }
        return *this;
    }

    //! Raw VkBuffer handle.
    [[nodiscard]] VkBuffer buffer() const
    {
        return m_buffer;
    }

    //! VMA allocation handle (for mapping, etc.).
    [[nodiscard]] VmaAllocation allocation() const
    {
        return m_allocation;
    }

    //! VMA allocation info (contains pMappedData for persistently mapped buffers).
    [[nodiscard]] VmaAllocationInfo allocationInfo() const
    {
        VmaAllocationInfo info{};
        vmaGetAllocationInfo(m_allocator, m_allocation, &info);
        return info;
    }

private:
    VmaAllocator m_allocator{VK_NULL_HANDLE}; //!< Non-owning reference to the parent allocator.
    VkBuffer m_buffer{VK_NULL_HANDLE}; //!< Owned buffer handle.
    VmaAllocation m_allocation{VK_NULL_HANDLE}; //!< Owned allocation handle.
};

//! RAII wrapper for a VMA image and its allocation.
class AllocatedImage {
public:
    //! Takes ownership of a VMA image+allocation pair.
    AllocatedImage(VmaAllocator allocator, VkImage image, VmaAllocation allocation) :
        m_allocator(allocator),
        m_image(image),
        m_allocation(allocation)
    {
    }

    ~AllocatedImage()
    {
        if (m_image != VK_NULL_HANDLE) {
            vmaDestroyImage(m_allocator, m_image, m_allocation);
        }
    }

    // Non-copyable
    AllocatedImage(const AllocatedImage&) = delete;
    AllocatedImage& operator=(const AllocatedImage&) = delete;

    // Movable
    AllocatedImage(AllocatedImage&& other) noexcept :
        m_allocator(other.m_allocator),
        m_image(other.m_image),
        m_allocation(other.m_allocation)
    {
        other.m_allocator = VK_NULL_HANDLE;
        other.m_image = VK_NULL_HANDLE;
        other.m_allocation = VK_NULL_HANDLE;
    }

    AllocatedImage& operator=(AllocatedImage&& other) noexcept
    {
        if (this != &other) {
            if (m_image != VK_NULL_HANDLE) {
                vmaDestroyImage(m_allocator, m_image, m_allocation);
            }
            m_allocator = other.m_allocator;
            m_image = other.m_image;
            m_allocation = other.m_allocation;
            other.m_allocator = VK_NULL_HANDLE;
            other.m_image = VK_NULL_HANDLE;
            other.m_allocation = VK_NULL_HANDLE;
        }
        return *this;
    }

    //! Raw VkImage handle.
    [[nodiscard]] VkImage image() const
    {
        return m_image;
    }

private:
    VmaAllocator m_allocator{VK_NULL_HANDLE}; //!< Non-owning reference to the parent allocator.
    VkImage m_image{VK_NULL_HANDLE}; //!< Owned image handle.
    VmaAllocation m_allocation{VK_NULL_HANDLE}; //!< Owned allocation handle.
};

//! RAII wrapper around VmaAllocator — creates and destroys the allocator automatically.
class Allocator {
public:
    //! Creates a VMA allocator using Volk function pointers.
    Allocator(const Instance& instance, const Device& device, LoggingLib::Logger& logger);

    ~Allocator();

    // Non-copyable, non-movable (VmaAllocator is not trivially relocatable)
    Allocator(const Allocator&) = delete;
    Allocator& operator=(const Allocator&) = delete;
    Allocator(Allocator&&) = delete;
    Allocator& operator=(Allocator&&) = delete;

    /*!
        Creates a GPU buffer with a VMA allocation.

        \param size Buffer size in bytes.
        \param buffer_usage Vulkan buffer usage flags.
        \param alloc_flags VMA allocation creation flags.
        \param memory_usage VMA memory usage hint.
        \return An RAII wrapper owning the buffer and its allocation.
    */
    [[nodiscard]] AllocatedBuffer createBuffer(VkDeviceSize size, VkBufferUsageFlags buffer_usage, VmaAllocationCreateFlags alloc_flags,
        VmaMemoryUsage memory_usage) const;

    /*!
        Creates a GPU image with a VMA allocation.

        \param width Image width in pixels.
        \param height Image height in pixels.
        \param format Vulkan image format.
        \param usage Vulkan image usage flags.
        \param mip_levels Number of mip levels (1 = no mip chain).
        \return An RAII wrapper owning the image and its allocation.
    */
    [[nodiscard]] AllocatedImage createImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, uint32_t mip_levels = 1) const;

    //! Raw VmaAllocator handle.
    [[nodiscard]] VmaAllocator handle() const
    {
        return m_allocator;
    }

private:
    LoggingLib::Logger& m_logger; //!< Logger reference (non-owning).
    VmaAllocator m_allocator{VK_NULL_HANDLE}; //!< Owned VMA allocator handle.
};
