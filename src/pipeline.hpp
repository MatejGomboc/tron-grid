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
#include <math/matrix.hpp>
#include <log/logger.hpp>
#include <cstdint>
#include <string>
#include <vector>

class Device; // forward declaration

//! Loads a SPIR-V binary from disc and returns it as a uint32_t vector.
[[nodiscard]] std::vector<uint32_t> loadSpirv(const std::string& path, LoggingLib::Logger& logger);

//! Returns the directory containing the running executable (with trailing separator).
[[nodiscard]] std::string executableDirectory();

//! Vertex layout — position (float3) + normal (float3) + UV (float2). Meshlet/RT-ready.
struct Vertex {
    float position[3]; //!< World-space position.
    float normal[3]; //!< Surface normal.
    float uv[2]; //!< Texture coordinates.
};

//! Camera uniform buffer — view and projection matrices, uploaded once per frame.
struct CameraUBO {
    MathLib::Mat4 view; //!< View matrix.
    MathLib::Mat4 projection; //!< Projection matrix.
};

/*!
    Owns the graphics pipeline, layout, descriptor set layout, descriptor pool,
    and per-frame descriptor sets. Uses dynamic rendering (no VkRenderPass).
*/
class Pipeline {
public:
    /*!
        Creates the graphics pipeline with depth testing, descriptors, and push constants.

        \param device The logical device.
        \param colour_format The swapchain colour attachment format.
        \param depth_format The depth attachment format.
        \param vert_spirv Vertex shader SPIR-V binary.
        \param frag_spirv Fragment shader SPIR-V binary.
        \param frames_in_flight Number of frames in flight (for per-frame descriptor sets).
        \param logger Logger reference.
    */
    Pipeline(const Device& device, vk::Format colour_format, vk::Format depth_format, const std::vector<uint32_t>& vert_spirv, const std::vector<uint32_t>& frag_spirv,
        uint32_t frames_in_flight, LoggingLib::Logger& logger);

    // Non-copyable, movable
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Pipeline(Pipeline&&) = default;
    Pipeline& operator=(Pipeline&&) = default;

    //! RAII pipeline handle.
    [[nodiscard]] const vk::raii::Pipeline& get() const
    {
        return m_pipeline;
    }

    //! Pipeline layout handle.
    [[nodiscard]] const vk::raii::PipelineLayout& layout() const
    {
        return m_layout;
    }

    //! Descriptor set for the given frame index.
    [[nodiscard]] vk::DescriptorSet descriptorSet(uint32_t frame_index) const
    {
        return *m_descriptor_sets[frame_index];
    }

    //! Binds a UBO buffer to the descriptor set for the given frame index.
    void bindUBO(uint32_t frame_index, VkBuffer buffer) const;

    //! Updates the camera UBO for the given frame index via its mapped pointer.
    void updateCameraUBO(uint32_t frame_index, const CameraUBO& ubo) const;

    //! Sets the mapped pointer for a frame's UBO (called once after buffer creation).
    void setUBOMappedPtr(uint32_t frame_index, void* ptr)
    {
        m_ubo_mapped_ptrs[frame_index] = ptr;
    }

private:
    const Device* m_device{nullptr}; //!< Non-owning device reference (for descriptor writes).
    LoggingLib::Logger& m_logger; //!< Logger reference (non-owning).
    vk::raii::DescriptorSetLayout m_descriptor_set_layout{nullptr}; //!< Descriptor set layout (binding 0 = camera UBO).
    vk::raii::PipelineLayout m_layout{nullptr}; //!< Pipeline layout (1 descriptor set + push constant range).
    vk::raii::Pipeline m_pipeline{nullptr}; //!< Graphics pipeline handle.
    vk::raii::DescriptorPool m_descriptor_pool{nullptr}; //!< Descriptor pool for per-frame sets.
    std::vector<vk::raii::DescriptorSet> m_descriptor_sets; //!< Per-frame descriptor sets.
    std::vector<void*> m_ubo_mapped_ptrs; //!< Persistently mapped UBO pointers (per frame).
};
