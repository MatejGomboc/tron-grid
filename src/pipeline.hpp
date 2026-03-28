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
    std::array<float, 3> position{}; //!< World-space position.
    std::array<float, 3> normal{}; //!< Surface normal.
    std::array<float, 2> uv{}; //!< Texture coordinates.
};

//! Per-object data stored in the SSBO — matches the Slang ObjectData struct.
struct ObjectData {
    MathLib::Mat4 model{}; //!< Model-to-world transform.
    uint32_t meshlet_offset{0}; //!< First meshlet index for this object's mesh.
    uint32_t meshlet_count{0}; //!< Number of meshlets for this object's mesh.
    uint32_t pad0{0}; //!< Padding to 16-byte alignment.
    uint32_t pad1{0}; //!< Padding to 16-byte alignment.
};

//! Per-object bounding sphere — matches the Slang ObjectBounds struct.
struct ObjectBounds {
    MathLib::Vec3 centre{}; //!< World-space bounding sphere centre.
    float radius{0.0f}; //!< Bounding sphere radius.
};

//! Camera uniform buffer — view and projection matrices + point light, uploaded once per frame.
struct CameraUBO {
    MathLib::Mat4 view{}; //!< View matrix.
    MathLib::Mat4 projection{}; //!< Projection matrix.
    MathLib::Vec3 light_pos{}; //!< Point light world-space position.
    float light_intensity{1.0f}; //!< Point light intensity (pre-multiplier before inverse square falloff).
};

//! Push constants for the task shader — frustum planes + object count.
struct TaskPushConstants {
    std::array<MathLib::Vec4, 6> planes{}; //!< Frustum planes (normals point inward).
    uint32_t object_count{0}; //!< Total number of objects.
};

/*!
    Owns the mesh shader pipeline, layout, descriptor sets, and per-frame resources.
    Uses dynamic rendering (no VkRenderPass). Task shader performs per-object frustum
    culling and dispatches mesh shader workgroups for visible objects.
*/
class Pipeline {
public:
    /*!
        Creates the mesh shader pipeline (task + mesh + fragment).

        \param device The logical device.
        \param colour_format The swapchain colour attachment format.
        \param depth_format The depth attachment format.
        \param task_spirv Task shader SPIR-V binary.
        \param mesh_frag_spirv Mesh + fragment shader SPIR-V binary (combined module).
        \param frames_in_flight Number of frames in flight (for per-frame descriptor sets).
        \param logger Logger reference.
    */
    Pipeline(const Device& device, vk::Format colour_format, vk::Format depth_format, const std::vector<uint32_t>& task_spirv,
        const std::vector<uint32_t>& mesh_frag_spirv, uint32_t frames_in_flight, LoggingLib::Logger& logger);

    // Non-copyable, non-movable.
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Pipeline(Pipeline&&) = delete;
    Pipeline& operator=(Pipeline&&) = delete;

    //! Mesh shader pipeline handle.
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

    //! Binds the camera UBO to the descriptor set for the given frame index.
    void bindUBO(uint32_t frame_index, VkBuffer buffer) const;

    //! Binds all mesh shader SSBOs to the descriptor set for the given frame index.
    void bindSSBOs(uint32_t frame_index, VkBuffer object_ssbo, VkDeviceSize object_size, VkBuffer bounds_ssbo, VkDeviceSize bounds_size, VkBuffer meshlet_desc_ssbo,
        VkDeviceSize meshlet_desc_size, VkBuffer vertex_ssbo, VkDeviceSize vertex_size, VkBuffer meshlet_vertex_indices_ssbo, VkDeviceSize meshlet_vertex_indices_size,
        VkBuffer meshlet_triangle_indices_ssbo, VkDeviceSize meshlet_triangle_indices_size) const;

    //! Binds the TLAS to descriptor binding 7 for the given frame index.
    void bindTLAS(uint32_t frame_index, vk::AccelerationStructureKHR tlas) const;

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

    vk::raii::DescriptorSetLayout m_descriptor_set_layout{nullptr}; //!< 8 bindings: UBO + 6 SSBOs + TLAS.
    vk::raii::PipelineLayout m_layout{nullptr}; //!< Pipeline layout (1 descriptor set + push constants).
    vk::raii::Pipeline m_pipeline{nullptr}; //!< Mesh shader pipeline handle.
    vk::raii::DescriptorPool m_descriptor_pool{nullptr}; //!< Descriptor pool.
    std::vector<vk::raii::DescriptorSet> m_descriptor_sets; //!< Per-frame descriptor sets.

    std::vector<void*> m_ubo_mapped_ptrs; //!< Persistently mapped UBO pointers (per frame).
};
