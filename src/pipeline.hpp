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
#include <math/vector.hpp>
#include <log/logger.hpp>
#include <array>
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
    std::array<float, 3> position; //!< World-space position.
    std::array<float, 3> normal; //!< Surface normal.
    std::array<float, 2> uv; //!< Texture coordinates.
};

//! Per-object data stored in the SSBO — matches the Slang ObjectData struct.
struct ObjectData {
    MathLib::Mat4 model; //!< Model-to-world transform.
};

//! Per-object bounding sphere — matches the Slang ObjectBounds struct.
struct ObjectBounds {
    MathLib::Vec3 centre; //!< World-space bounding sphere centre.
    float radius{0.0f}; //!< Bounding sphere radius.
};

//! Camera uniform buffer — view and projection matrices, uploaded once per frame.
struct CameraUBO {
    MathLib::Mat4 view; //!< View matrix.
    MathLib::Mat4 projection; //!< Projection matrix.
};

//! Push constants for the compute culling shader.
struct CullPushConstants {
    MathLib::Vec4 planes[6]; //!< Frustum planes (normals point inward).
    uint32_t object_count{0}; //!< Total number of objects to cull.
};

/*!
    Owns the graphics pipeline, compute culling pipeline, layouts, descriptor sets,
    and per-frame descriptor resources. Uses dynamic rendering (no VkRenderPass).
*/
class Pipeline {
public:
    /*!
        Creates the graphics and compute pipelines.

        \param device The logical device.
        \param colour_format The swapchain colour attachment format.
        \param depth_format The depth attachment format.
        \param vert_spirv Vertex shader SPIR-V binary.
        \param frag_spirv Fragment shader SPIR-V binary.
        \param cull_spirv Compute culling shader SPIR-V binary.
        \param frames_in_flight Number of frames in flight (for per-frame descriptor sets).
        \param logger Logger reference.
    */
    Pipeline(const Device& device, vk::Format colour_format, vk::Format depth_format, const std::vector<uint32_t>& vert_spirv, const std::vector<uint32_t>& frag_spirv,
        const std::vector<uint32_t>& cull_spirv, uint32_t frames_in_flight, LoggingLib::Logger& logger);

    // Non-copyable, movable
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Pipeline(Pipeline&&) = default;
    Pipeline& operator=(Pipeline&&) = default;

    //! Graphics pipeline handle.
    [[nodiscard]] const vk::raii::Pipeline& get() const
    {
        return m_graphics_pipeline;
    }

    //! Graphics pipeline layout handle.
    [[nodiscard]] const vk::raii::PipelineLayout& layout() const
    {
        return m_graphics_layout;
    }

    //! Compute culling pipeline handle.
    [[nodiscard]] const vk::raii::Pipeline& cullPipeline() const
    {
        return m_compute_pipeline;
    }

    //! Compute pipeline layout handle.
    [[nodiscard]] const vk::raii::PipelineLayout& cullLayout() const
    {
        return m_compute_layout;
    }

    //! Graphics descriptor set for the given frame index.
    [[nodiscard]] vk::DescriptorSet graphicsDescriptorSet(uint32_t frame_index) const
    {
        return *m_graphics_descriptor_sets[frame_index];
    }

    //! Compute descriptor set (shared across frames — static data).
    [[nodiscard]] vk::DescriptorSet computeDescriptorSet() const
    {
        return *m_compute_descriptor_sets[0];
    }

    //! Binds a UBO buffer to the graphics descriptor set for the given frame index.
    void bindUBO(uint32_t frame_index, VkBuffer buffer) const;

    //! Binds an object SSBO to the graphics descriptor set for the given frame index.
    void bindObjectSSBO(uint32_t frame_index, VkBuffer buffer, VkDeviceSize size) const;

    //! Binds the visible indices SSBO to the graphics descriptor set for the given frame index.
    void bindVisibleIndices(uint32_t frame_index, VkBuffer buffer, VkDeviceSize size) const;

    //! Binds compute descriptor set resources (bounds, draw commands, draw count, visible indices).
    void bindComputeResources(VkBuffer bounds_buffer, VkDeviceSize bounds_size, VkBuffer indirect_buffer, VkDeviceSize indirect_size, VkBuffer draw_count_buffer,
        VkDeviceSize draw_count_size, VkBuffer visible_indices_buffer, VkDeviceSize visible_indices_size) const;

    //! Updates the camera UBO for the given frame index via its mapped pointer.
    void updateCameraUBO(uint32_t frame_index, const CameraUBO& ubo) const;

    //! Sets the mapped pointer for a frame's UBO (called once after buffer creation).
    void setUBOMappedPtr(uint32_t frame_index, void* ptr)
    {
        m_ubo_mapped_ptrs[frame_index] = ptr;
    }

private:
    const Device* m_device{nullptr}; //!< Non-owning device reference.
    LoggingLib::Logger& m_logger; //!< Logger reference (non-owning).

    // Graphics pipeline
    vk::raii::DescriptorSetLayout m_graphics_descriptor_set_layout{nullptr}; //!< Binding 0 = UBO, 1 = SSBO, 2 = visible indices.
    vk::raii::PipelineLayout m_graphics_layout{nullptr}; //!< Graphics pipeline layout.
    vk::raii::Pipeline m_graphics_pipeline{nullptr}; //!< Graphics pipeline handle.
    vk::raii::DescriptorPool m_graphics_descriptor_pool{nullptr}; //!< Graphics descriptor pool.
    std::vector<vk::raii::DescriptorSet> m_graphics_descriptor_sets; //!< Per-frame graphics descriptor sets.

    // Compute pipeline
    vk::raii::DescriptorSetLayout m_compute_descriptor_set_layout{nullptr}; //!< Binding 0 = bounds, 1 = draw cmds, 2 = draw count, 3 = visible indices.
    vk::raii::PipelineLayout m_compute_layout{nullptr}; //!< Compute pipeline layout (push constants for frustum).
    vk::raii::Pipeline m_compute_pipeline{nullptr}; //!< Compute culling pipeline handle.
    vk::raii::DescriptorPool m_compute_descriptor_pool{nullptr}; //!< Compute descriptor pool.
    std::vector<vk::raii::DescriptorSet> m_compute_descriptor_sets; //!< Compute descriptor sets.

    // Per-frame UBO
    std::vector<void*> m_ubo_mapped_ptrs; //!< Persistently mapped UBO pointers (per frame).
};
