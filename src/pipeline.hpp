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

//! Vertex layout — position + flat normal + UV + smooth normal. Meshlet/RT-ready.
struct Vertex {
    std::array<float, 3> position{}; //!< World-space position.
    std::array<float, 3> normal{}; //!< Per-face flat normal (for shading).
    std::array<float, 2> uv{}; //!< Texture coordinates (uv.x = diagonal suppression flag).
    std::array<float, 3> smooth_normal{}; //!< Per-vertex smooth normal (for reflections).
    float vertex_pad{0.0f}; //!< Padding to 16-byte alignment (48 bytes total).
};

//! Per-object data stored in the SSBO — matches the Slang ObjectData struct.
struct ObjectData {
    MathLib::Mat4 model{}; //!< Model-to-world transform.
    uint32_t meshlet_offset{0}; //!< First meshlet index for this object's mesh.
    uint32_t meshlet_count{0}; //!< Number of meshlets for this object's mesh.
    uint32_t material_index{0}; //!< Index into the material SSBO.
    uint32_t pad1{0}; //!< Padding to 16-byte alignment.
};

//! PBR material properties stored in the material SSBO — matches the Slang MaterialData struct.
struct Material {
    MathLib::Vec3 base_colour{}; //!< Albedo / diffuse colour.
    float roughness{0.5f}; //!< Perceptual roughness [0, 1].
    MathLib::Vec3 emissive{}; //!< Self-illumination colour.
    float emissive_strength{0.0f}; //!< HDR emissive multiplier.
    float metallic{0.0f}; //!< Metalness [0, 1].
    float ior{1.5f}; //!< Index of refraction (for Fresnel F0 computation).
    float opacity{1.0f}; //!< 1.0 = opaque, <1.0 = translucent (Phase 8).
    float pad{0.0f}; //!< Padding to 16-byte alignment (48 bytes total).
};

//! Per-object bounding sphere — matches the Slang ObjectBounds struct.
struct ObjectBounds {
    MathLib::Vec3 centre{}; //!< World-space bounding sphere centre.
    float radius{0.0f}; //!< Bounding sphere radius.
};

//! Camera uniform buffer — view/projection matrices + camera state, uploaded once per frame.
//! Layout MUST match the Slang `CameraData` structs in mesh.slang and skybox.slang
//! byte-for-byte (scalar layout enforced device-wide by VkPhysicalDeviceVulkan12Features::scalarBlockLayout).
struct CameraUBO {
    MathLib::Mat4 view{}; //!< View matrix.
    MathLib::Mat4 projection{}; //!< Projection matrix.
    MathLib::Mat4 inv_view_projection{}; //!< Inverse view-projection matrix (for skybox ray reconstruction).
    MathLib::Mat4 prev_view_projection{}; //!< Previous frame's view-projection (for motion vectors).
    MathLib::Vec3 camera_pos{}; //!< Camera world-space position (for view vector in PBR).
    uint32_t frame_count{0}; //!< Frame counter (for pseudo-random sampling in emissive lighting).
    uint32_t emissive_count{0}; //!< Number of emissive triangles in the emissive SSBO.
    float total_emissive_power{0.0f}; //!< Sum of all emissive triangle powers (for PDF normalisation).
    uint32_t screen_width{0}; //!< Render target width in pixels (for reservoir indexing).
    uint32_t screen_height{0}; //!< Render target height in pixels (for reservoir indexing).
    float time{0.0f}; //!< Elapsed time in seconds; drives sky-cloud animation in skybox.slang and mesh.slang's reflection-miss sky sampling.
};

//! Emissive triangle for area light sampling — matches the Slang EmissiveTriangle struct.
struct EmissiveTriangle {
    MathLib::Vec3 v0{}; //!< Triangle vertex 0.
    float area{0.0f}; //!< Triangle surface area.
    MathLib::Vec3 v1{}; //!< Triangle vertex 1.
    float cdf{0.0f}; //!< Cumulative distribution function value (power-weighted).
    MathLib::Vec3 v2{}; //!< Triangle vertex 2.
    float emissive_pad0{0.0f}; //!< Padding.
    MathLib::Vec3 emissive{}; //!< Emissive radiance (colour × strength).
    float emissive_pad1{0.0f}; //!< Padding.
};

//! Per-pixel reservoir for ReSTIR temporal reuse — matches the Slang Reservoir struct.
struct Reservoir {
    MathLib::Vec3 y_pos{}; //!< Selected light sample position.
    float w_sum{0.0f}; //!< Sum of RIS weights.
    MathLib::Vec3 y_emissive{}; //!< Emissive radiance at the selected sample.
    uint32_t M{0}; //!< Number of candidates merged; clamped proportionally (with w_sum) during temporal/spatial merges.
    MathLib::Vec3 y_normal{}; //!< Light surface normal at the selected sample.
    float W{0.0f}; //!< Final contribution weight: w_sum / (M × p_hat(y)).
    MathLib::Vec3 indirect{}; //!< Accumulated indirect radiance (single-bounce GI).
    float ao{0.0f}; //!< Accumulated ambient-occlusion visibility in [0, 1] (1 = fully lit, 0 = fully occluded).
    MathLib::Vec3 shading_pos{}; //!< Shading-surface world position of the pixel that wrote this reservoir.
    uint32_t shading_normal_oct{0}; //!< Shading-surface normal, octahedral-packed to 32 bits.
};

//! Push constants for the task shader — frustum planes + object range.
struct TaskPushConstants {
    std::array<MathLib::Vec4, 6> planes{}; //!< Frustum planes (normals point inward).
    uint32_t object_count{0}; //!< Number of objects in this dispatch (the dispatch covers entities [base, base + count)).
    uint32_t base_object_index{0}; //!< First object SSBO index — lets the same pipeline render any contiguous range (opaque vs transparent partitioning).
};

/*!
    Owns the mesh shader pipelines, layout, descriptor sets, and per-frame resources.
    Uses dynamic rendering (no VkRenderPass). Task shader performs per-object frustum
    culling and dispatches mesh shader workgroups for visible objects.

    Two pipeline variants share the same descriptor set layout, pipeline layout, and
    task/mesh stages. Only the fragment stage and blend/depth-write state differ:

    - Opaque pipeline (Phase 8 Etape 40): fragMain, depth write on, no blending.
        Renders entities whose material has opacity == 1 in the first dispatch.
    - Transparent pipeline (Phase 8 Etape 40): fragTransparent, depth write off,
        premultiplied alpha blending. Renders entities whose material has opacity < 1
        in the second dispatch.
*/
class Pipeline {
public:
    /*!
        Creates the mesh shader pipelines (opaque + transparent variants).

        \param device The logical device.
        \param colour_format The swapchain colour attachment format.
        \param depth_format The depth attachment format.
        \param sample_count MSAA sample count for rasterisation (enables full sample-rate shading).
        \param task_spirv Task shader SPIR-V binary.
        \param mesh_frag_spirv Mesh + fragment shader SPIR-V binary (combined module containing meshMain, fragMain, fragTransparent entry points).
        \param frames_in_flight Number of frames in flight (for per-frame descriptor sets).
        \param logger Logger reference.
    */
    Pipeline(const Device& device, vk::Format colour_format, vk::Format depth_format, vk::SampleCountFlagBits sample_count, const std::vector<uint32_t>& task_spirv,
        const std::vector<uint32_t>& mesh_frag_spirv, uint32_t frames_in_flight, LoggingLib::Logger& logger);

    // Non-copyable, non-movable.
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Pipeline(Pipeline&&) = delete;
    Pipeline& operator=(Pipeline&&) = delete;

    //! Opaque mesh shader pipeline handle (fragMain, depth write on, no blending).
    [[nodiscard]] const vk::raii::Pipeline& opaquePipeline() const
    {
        return m_opaque_pipeline;
    }

    //! Transparent mesh shader pipeline handle (fragTransparent, depth test only, premultiplied alpha blending).
    [[nodiscard]] const vk::raii::Pipeline& transparentPipeline() const
    {
        return m_transparent_pipeline;
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

    //! Binds the material SSBO to descriptor binding 8 for the given frame index.
    void bindMaterialSSBO(uint32_t frame_index, VkBuffer buffer, VkDeviceSize size) const;

    //! Binds the emissive triangle SSBO to descriptor binding 9 for the given frame index.
    void bindEmissiveSSBO(uint32_t frame_index, VkBuffer buffer, VkDeviceSize size) const;

    //! Binds reservoir ping-pong buffers to bindings 10 (write) and 11 (read) for the given frame index.
    void bindReservoirBuffers(uint32_t frame_index, VkBuffer write_buffer, VkDeviceSize write_size, VkBuffer read_buffer, VkDeviceSize read_size) const;

    //! Binds the previous frame's denoised indirect-GI history texture to binding 12.
    //! Phase 8 Etape 42c-polish-3 (SVGF). The view should point at whichever ping-pong
    //! image holds the *previous* frame's SVGF output; the host alternates the choice
    //! between two physical images each frame. Image must be in eGeneral layout when
    //! sampled (the SVGF compute pass writes it in eGeneral and never transitions away).
    void bindDenoisedIndirectHistory(uint32_t frame_index, vk::ImageView view) const;

    //! Binds the lighting_raw scratch texture to binding 14 (mesh fragment write target,
    //! SVGF input). Phase 8 Etape 42c-polish-3. Single image shared across frames (no
    //! ping-pong needed because mesh shader writes it then SVGF reads it within the same
    //! frame, then it's discarded). Image must be in eGeneral layout.
    void bindLightingRaw(uint32_t frame_index, vk::ImageView view) const;

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

    vk::raii::DescriptorSetLayout m_descriptor_set_layout{nullptr}; //!< 14 bindings: UBO + 10 SSBOs + TLAS + 2 SVGF storage images (history read + lighting_raw write).
    vk::raii::PipelineLayout m_layout{nullptr}; //!< Pipeline layout (1 descriptor set + push constants).
    vk::raii::Pipeline m_opaque_pipeline{nullptr}; //!< Opaque mesh shader pipeline (fragMain).
    vk::raii::Pipeline m_transparent_pipeline{nullptr}; //!< Transparent mesh shader pipeline (fragTransparent, alpha blending).
    vk::raii::DescriptorPool m_descriptor_pool{nullptr}; //!< Descriptor pool.
    std::vector<vk::raii::DescriptorSet> m_descriptor_sets{}; //!< Per-frame descriptor sets.

    std::vector<void*> m_ubo_mapped_ptrs{}; //!< Persistently mapped UBO pointers (per frame).
};
