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
#include "camera.hpp"
#include "components.hpp"
#include "device.hpp"
#include "instance.hpp"
#include "meshlet.hpp"
#include "pipeline.hpp"
#include "scene.hpp"
#include "surface.hpp"
#include "swapchain.hpp"
#include "terrain.hpp"
#include <log/logger.hpp>
#include <math/matrix.hpp>
#include <math/projection.hpp>
#include <math/vector.hpp>
#include <signal/signal.hpp>
#include <window/window.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

//! Event sent from the main (event) thread to the render thread via Signal<T>.
struct RenderEvent {
    //! Discriminator for the render event type.
    enum class Type {
        Render, //!< Window needs redrawing (expose, initial frame).
        Resize, //!< Window was resized — recreate swapchain before rendering.
        KeyDown, //!< Key was pressed.
        KeyUp, //!< Key was released.
        MouseMove, //!< Mouse cursor moved.
        MouseButton, //!< Mouse button pressed or released.
        Stop //!< Render thread should shut down.
    };

    Type type{Type::Render}; //!< Event type.
    uint32_t width{0}; //!< Resize width / keycode / button index.
    uint32_t height{0}; //!< Resize height.
    int32_t dx{0}; //!< Mouse delta X.
    int32_t dy{0}; //!< Mouse delta Y.
    bool pressed{false}; //!< True if button was pressed (MouseButton only).
};

//! Maximum number of frames that can be in-flight simultaneously.
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

//! Depth buffer format used throughout.
constexpr vk::Format DEPTH_FORMAT = vk::Format::eD32Sfloat;

//! HDR colour buffer format — float16 for emissive values above 1.0.
constexpr vk::Format HDR_FORMAT = vk::Format::eR16G16B16A16Sfloat;

//! Camera movement speed (metres per second).
constexpr float CAMERA_SPEED = 5.0f;

//! Camera mouse sensitivity (radians per pixel).
constexpr float MOUSE_SENSITIVITY{0.003f};

//! Post-process compute shader workgroup size (must match postprocess.slang numthreads).
constexpr uint32_t PP_WORKGROUP_SIZE{8};

//! Maximum number of mip levels in the bloom downsample chain.
constexpr uint32_t BLOOM_MAX_MIPS{6};

//! HDR luminance threshold for bloom extraction — only pixels brighter than this contribute.
constexpr float BLOOM_THRESHOLD{1.0f};

//! Bloom composite strength — scales the additive bloom contribution before tonemapping.
constexpr float BLOOM_STRENGTH{0.095f};

//! Fence poll timeout — short enough for responsive shutdown, long enough to avoid busy-waiting.
constexpr uint64_t FENCE_TIMEOUT_NS{100'000'000};

//! Volumetric froxel grid dimensions (Phase 8 Etape 41 sub-etape 41a).
//! Frustum-aligned: x/y are screen-space tiles, z is logarithmically spaced view-space depth.
//! 160 × 90 × 64 = 921 600 froxels; at rgba16f = 8 bytes/froxel → 7.4 MB total. Cheap.
constexpr uint32_t FROXEL_WIDTH{160};
constexpr uint32_t FROXEL_HEIGHT{90};
constexpr uint32_t FROXEL_DEPTH{64};

//! Volumetric near/far in view-space metres. The grid spans [near, far] along z, with
//! the rest of the depth range (beyond FROXEL_FAR) rendered without volumetric fog.
constexpr float FROXEL_NEAR{0.1f};
constexpr float FROXEL_FAR{120.0f};

//! Base extinction coefficient (m^-1) at ground level. Subtle uniform fog for 41a; later
//! sub-etapes will keep this as the baseline density underneath emissive light scattering.
constexpr float FOG_DENSITY{0.012f};

//! World-space y where the height-falloff fog density starts to attenuate.
constexpr float FOG_HEIGHT_BASE{4.0f};

//! Exponential falloff scale (metres). Density at height (FOG_HEIGHT_BASE + h) is
//! FOG_DENSITY * exp(-h / FOG_HEIGHT_FALLOFF).
constexpr float FOG_HEIGHT_FALLOFF{6.0f};

//! Volumetric inject compute shader push-constant block. Layout MUST match the slang
//! FroxelPushConstants struct in src/inject_density.slang byte-for-byte (scalar layout).
struct VolumetricInjectPushConstants {
    uint32_t froxel_width{0};
    uint32_t froxel_height{0};
    uint32_t froxel_depth{0};
    float fog_density{0.0f};
    float fog_height_base{0.0f};
    float fog_height_falloff{0.0f};
    float near_plane{0.0f};
    float far_plane{0.0f};
    MathLib::Mat4 inv_view_projection{}; //!< World reconstruction at froxel centres.
    MathLib::Vec3 camera_pos{};
    float pad{0.0f};
};

//! Volumetric composite compute shader push-constant block. Layout MUST match the slang
//! VolumetricPushConstants struct in src/volumetric_composite.slang.
struct VolumetricCompositePushConstants {
    uint32_t screen_width{0};
    uint32_t screen_height{0};
    uint32_t froxel_width{0};
    uint32_t froxel_height{0};
    uint32_t froxel_depth{0};
    float pad0{0.0f};
    float pad1{0.0f};
    float pad2{0.0f};
};

//! Computes the number of mip levels for the bloom texture (base = half swapchain resolution).
[[nodiscard]] static uint32_t bloomMipCount(vk::Extent2D extent)
{
    uint32_t half_w{std::max(extent.width / 2, 1u)};
    uint32_t half_h{std::max(extent.height / 2, 1u)};
    uint32_t levels{static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(half_w, half_h))))) + 1};
    return std::min(levels, BLOOM_MAX_MIPS);
}

//! Platform-specific key codes for WASD, Space, Shift, and right mouse button.
#ifdef _WIN32
constexpr uint32_t KEY_W{0x57};
constexpr uint32_t KEY_A{0x41};
constexpr uint32_t KEY_S{0x53};
constexpr uint32_t KEY_D{0x44};
constexpr uint32_t KEY_SPACE{0x20};
constexpr uint32_t KEY_SHIFT{0x10};
constexpr uint32_t KEY_ESC{27};
constexpr uint32_t MOUSE_RIGHT{1};
#else
constexpr uint32_t KEY_W{25};
constexpr uint32_t KEY_A{38};
constexpr uint32_t KEY_S{39};
constexpr uint32_t KEY_D{40};
constexpr uint32_t KEY_SPACE{65};
constexpr uint32_t KEY_SHIFT{50};
constexpr uint32_t KEY_ESC{9};
constexpr uint32_t MOUSE_RIGHT{1};
#endif

/*!
    Records bloom extraction (mip 0) and downsample passes (mips 1..N-1) into
    the command buffer. The bloom image must already be in GENERAL layout.
    Each mip transition is synchronised with a per-mip barrier.
*/
static void recordBloomDownsample(const vk::raii::CommandBuffer& cmd, vk::Image bloom_image, uint32_t mip_count, uint32_t base_w, uint32_t base_h,
    vk::Pipeline extract_pipeline, vk::Pipeline downsample_pipeline, vk::PipelineLayout bloom_layout, const std::vector<vk::DescriptorSet>& bloom_sets, float threshold)
{
    vk::ImageSubresourceRange colour_range{};
    colour_range.aspectMask = vk::ImageAspectFlagBits::eColor;
    colour_range.baseMipLevel = 0;
    colour_range.levelCount = 1;
    colour_range.baseArrayLayer = 0;
    colour_range.layerCount = 1;

    // ── Extraction pass: HDR → bloom mip 0 ──

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, extract_pipeline);
    cmd.pushConstants<float>(bloom_layout, vk::ShaderStageFlagBits::eCompute, 0, threshold);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, bloom_layout, 0, {bloom_sets[0]}, {});

    uint32_t group_x{(base_w + PP_WORKGROUP_SIZE - 1) / PP_WORKGROUP_SIZE};
    uint32_t group_y{(base_h + PP_WORKGROUP_SIZE - 1) / PP_WORKGROUP_SIZE};
    cmd.dispatch(group_x, group_y, 1);

    // ── Downsample passes: bloom mip i-1 → bloom mip i ──

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, downsample_pipeline);

    for (uint32_t i{1}; i < mip_count; ++i) {
        // Barrier: previous mip write must complete before this mip reads it.
        vk::ImageMemoryBarrier2 mip_barrier{};
        mip_barrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        mip_barrier.srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
        mip_barrier.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        mip_barrier.dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead;
        mip_barrier.oldLayout = vk::ImageLayout::eGeneral;
        mip_barrier.newLayout = vk::ImageLayout::eGeneral;
        mip_barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
        mip_barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
        mip_barrier.image = bloom_image;
        mip_barrier.subresourceRange = colour_range;
        mip_barrier.subresourceRange.baseMipLevel = i - 1;
        mip_barrier.subresourceRange.levelCount = 1;

        vk::DependencyInfo dep{};
        dep.setImageMemoryBarriers(mip_barrier);
        cmd.pipelineBarrier2(dep);

        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, bloom_layout, 0, {bloom_sets[i]}, {});

        uint32_t mip_w{std::max(base_w >> i, 1u)};
        uint32_t mip_h{std::max(base_h >> i, 1u)};
        uint32_t gx{(mip_w + PP_WORKGROUP_SIZE - 1) / PP_WORKGROUP_SIZE};
        uint32_t gy{(mip_h + PP_WORKGROUP_SIZE - 1) / PP_WORKGROUP_SIZE};
        cmd.dispatch(gx, gy, 1);
    }
}

/*!
    Records bloom upsample passes (smallest mip → mip 0) into the command buffer.
    Each pass reads from a smaller mip and additively blends into the next larger mip.
    The bloom image must already be in GENERAL layout with downsample writes complete.
*/
static void recordBloomUpsample(const vk::raii::CommandBuffer& cmd, vk::Image bloom_image, uint32_t mip_count, uint32_t base_w, uint32_t base_h,
    vk::Pipeline upsample_pipeline, vk::PipelineLayout bloom_layout, const std::vector<vk::DescriptorSet>& upsample_sets)
{
    if (mip_count < 2) {
        return;
    }

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, upsample_pipeline);

    // Upsample from mip (mip_count - 2) down to mip 0.
    // At step i, we read from mip (i + 1) and read+write mip i (additive blend).
    for (uint32_t i{mip_count - 2};; --i) {
        // Barrier on source mip (i + 1): prior writes must complete before read.
        vk::ImageMemoryBarrier2 src_barrier{};
        src_barrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        src_barrier.srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
        src_barrier.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        src_barrier.dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead;
        src_barrier.oldLayout = vk::ImageLayout::eGeneral;
        src_barrier.newLayout = vk::ImageLayout::eGeneral;
        src_barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
        src_barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
        src_barrier.image = bloom_image;
        src_barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        src_barrier.subresourceRange.baseMipLevel = i + 1;
        src_barrier.subresourceRange.levelCount = 1;
        src_barrier.subresourceRange.baseArrayLayer = 0;
        src_barrier.subresourceRange.layerCount = 1;

        // Barrier on destination mip (i): prior writes must complete before read+write
        // (upsample reads existing value for additive blend, then writes the sum).
        vk::ImageMemoryBarrier2 dst_barrier{};
        dst_barrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        dst_barrier.srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
        dst_barrier.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        dst_barrier.dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite;
        dst_barrier.oldLayout = vk::ImageLayout::eGeneral;
        dst_barrier.newLayout = vk::ImageLayout::eGeneral;
        dst_barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
        dst_barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
        dst_barrier.image = bloom_image;
        dst_barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        dst_barrier.subresourceRange.baseMipLevel = i;
        dst_barrier.subresourceRange.levelCount = 1;
        dst_barrier.subresourceRange.baseArrayLayer = 0;
        dst_barrier.subresourceRange.layerCount = 1;

        std::array<vk::ImageMemoryBarrier2, 2> upsample_barriers{src_barrier, dst_barrier};
        vk::DependencyInfo dep{};
        dep.setImageMemoryBarriers(upsample_barriers);
        cmd.pipelineBarrier2(dep);

        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, bloom_layout, 0, {upsample_sets[i]}, {});

        uint32_t mip_w{std::max(base_w >> i, 1u)};
        uint32_t mip_h{std::max(base_h >> i, 1u)};
        uint32_t gx{(mip_w + PP_WORKGROUP_SIZE - 1) / PP_WORKGROUP_SIZE};
        uint32_t gy{(mip_h + PP_WORKGROUP_SIZE - 1) / PP_WORKGROUP_SIZE};
        cmd.dispatch(gx, gy, 1);

        if (i == 0) {
            break;
        }
    }
}

/*!
    Records a command buffer with mesh shader rendering and compute post-processing.
    The task shader performs per-object frustum culling and dispatches mesh shader
    workgroups for visible objects. Bloom extraction, downsample, and upsample
    run between the scene render and the tonemap pass. The post-process shader
    composites bloom with the HDR image before tonemapping.

    A cross-frame buffer memory barrier at the very top makes the previous
    submission's reservoir writes visible to this frame's reservoir reads — without
    it, the ping-pong ReSTIR reads tear/stale data across frames even though the
    per-frame fence provides execution order, because Vulkan submission-order alone
    does not guarantee memory availability across batches.
*/
static void recordFrame(const vk::raii::CommandBuffer& cmd, vk::Image msaa_image, vk::ImageView msaa_view, vk::Image hdr_image, vk::ImageView hdr_view,
    vk::ImageView depth_view, vk::Image depth_image, vk::Image swapchain_image, vk::Extent2D extent, const Pipeline& pipeline, vk::DescriptorSet descriptor_set,
    const MathLib::Frustum& frustum, uint32_t opaque_object_count, uint32_t transparent_object_count, vk::Pipeline pp_pipeline, vk::PipelineLayout pp_layout,
    vk::DescriptorSet pp_descriptor_set, vk::Image bloom_image, uint32_t bloom_mip_count, uint32_t bloom_base_w, uint32_t bloom_base_h,
    vk::Pipeline bloom_extract_pipeline, vk::Pipeline bloom_downsample_pipeline, vk::Pipeline bloom_upsample_pipeline, vk::PipelineLayout bloom_layout,
    const std::vector<vk::DescriptorSet>& bloom_sets, const std::vector<vk::DescriptorSet>& bloom_upsample_sets, vk::Pipeline skybox_pipeline,
    vk::PipelineLayout skybox_layout, vk::DescriptorSet skybox_descriptor_set, vk::Pipeline inject_pipeline, vk::PipelineLayout inject_pipeline_layout,
    vk::DescriptorSet inject_descriptor_set, vk::Pipeline composite_pipeline, vk::PipelineLayout composite_pipeline_layout, vk::DescriptorSet composite_descriptor_set,
    vk::Image froxel_image, const VolumetricInjectPushConstants& inject_push, float time)
{
    vk::CommandBufferBeginInfo begin_info{};
    begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd.begin(begin_info);

    // ── Cross-frame reservoir memory barrier ──
    // The previous submission's fragment shader wrote the ReSTIR reservoir ping-pong
    // buffers; this submission's fragment shader will read them. Submission order
    // provides execution ordering, but per the Vulkan memory model we still need an
    // explicit memory barrier for the write-to-read hand-off to be visible.
    vk::MemoryBarrier2 reservoir_cross_frame{};
    reservoir_cross_frame.srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
    reservoir_cross_frame.srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
    reservoir_cross_frame.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
    reservoir_cross_frame.dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite;

    vk::DependencyInfo dep_reservoir{};
    dep_reservoir.setMemoryBarriers(reservoir_cross_frame);
    cmd.pipelineBarrier2(dep_reservoir);

    // ── Transition HDR + depth to render targets ──

    vk::ImageSubresourceRange colour_range{};
    colour_range.aspectMask = vk::ImageAspectFlagBits::eColor;
    colour_range.baseMipLevel = 0;
    colour_range.levelCount = 1;
    colour_range.baseArrayLayer = 0;
    colour_range.layerCount = 1;

    vk::ImageSubresourceRange depth_range{};
    depth_range.aspectMask = vk::ImageAspectFlagBits::eDepth;
    depth_range.baseMipLevel = 0;
    depth_range.levelCount = 1;
    depth_range.baseArrayLayer = 0;
    depth_range.layerCount = 1;

    // Transition HDR image: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL (discard prior content).
    // srcStage includes eColorAttachmentOutput because MSAA resolve writes to HDR during endRendering,
    // and eComputeShader because the previous frame's post-process reads HDR in GENERAL layout.
    // srcAccess lists only writes — reads are covered by execution ordering via srcStage alone.
    vk::ImageMemoryBarrier2 hdr_to_render{};
    hdr_to_render.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    hdr_to_render.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
    hdr_to_render.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    hdr_to_render.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
    hdr_to_render.oldLayout = vk::ImageLayout::eUndefined;
    hdr_to_render.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
    hdr_to_render.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    hdr_to_render.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    hdr_to_render.image = hdr_image;
    hdr_to_render.subresourceRange = colour_range;

    // Transition MSAA colour: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL (transient, always discard).
    // srcStage covers the previous frame's render which wrote to the MSAA image.
    vk::ImageMemoryBarrier2 msaa_to_render{};
    msaa_to_render.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    msaa_to_render.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
    msaa_to_render.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    msaa_to_render.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
    msaa_to_render.oldLayout = vk::ImageLayout::eUndefined;
    msaa_to_render.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
    msaa_to_render.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    msaa_to_render.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    msaa_to_render.image = msaa_image;
    msaa_to_render.subresourceRange = colour_range;

    // Transition depth: UNDEFINED → DEPTH_ATTACHMENT_OPTIMAL.
    vk::ImageMemoryBarrier2 depth_to_render{};
    depth_to_render.srcStageMask = vk::PipelineStageFlagBits2::eLateFragmentTests;
    depth_to_render.srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    depth_to_render.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests;
    depth_to_render.dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite | vk::AccessFlagBits2::eDepthStencilAttachmentRead;
    depth_to_render.oldLayout = vk::ImageLayout::eUndefined;
    depth_to_render.newLayout = vk::ImageLayout::eDepthAttachmentOptimal;
    depth_to_render.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    depth_to_render.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    depth_to_render.image = depth_image;
    depth_to_render.subresourceRange = depth_range;

    std::array<vk::ImageMemoryBarrier2, 3> to_render_barriers{msaa_to_render, hdr_to_render, depth_to_render};
    vk::DependencyInfo dep_to_render{};
    dep_to_render.setImageMemoryBarriers(to_render_barriers);
    cmd.pipelineBarrier2(dep_to_render);

    // ── Render scene to HDR image ──

    vk::RenderingAttachmentInfo colour_attachment{};
    colour_attachment.imageView = msaa_view;
    colour_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    colour_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    colour_attachment.storeOp = vk::AttachmentStoreOp::eDontCare;
    colour_attachment.clearValue.color = vk::ClearColorValue{std::array{0.01f, 0.01f, 0.02f, 1.0f}};
    colour_attachment.resolveMode = vk::ResolveModeFlagBits::eAverage;
    colour_attachment.resolveImageView = hdr_view;
    colour_attachment.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::RenderingAttachmentInfo depth_attachment{};
    depth_attachment.imageView = depth_view;
    depth_attachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
    depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    depth_attachment.storeOp = vk::AttachmentStoreOp::eDontCare;
    depth_attachment.clearValue.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    vk::RenderingInfo rendering_info{};
    rendering_info.renderArea.offset = vk::Offset2D{0, 0};
    rendering_info.renderArea.extent = extent;
    rendering_info.layerCount = 1;
    rendering_info.setColorAttachments(colour_attachment);
    rendering_info.setPDepthAttachment(&depth_attachment);

    cmd.beginRendering(rendering_info);

    vk::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    cmd.setViewport(0, {viewport});

    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D{0, 0};
    scissor.extent = extent;
    cmd.setScissor(0, {scissor});

    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline.layout(), 0, {descriptor_set}, {});

    // ── Opaque pass: render entities [0, opaque_object_count) ──
    // Reservoir writes happen here. Depth is written so the transparent pass can test
    // against the opaque silhouette.
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.opaquePipeline());

    TaskPushConstants opaque_push{};
    opaque_push.planes = frustum.planes;
    opaque_push.object_count = opaque_object_count;
    opaque_push.base_object_index = 0;
    cmd.pushConstants<TaskPushConstants>(*pipeline.layout(), vk::ShaderStageFlagBits::eTaskEXT, 0, opaque_push);

    if (opaque_object_count > 0) {
        cmd.drawMeshTasksEXT(opaque_object_count, 1, 1);
    }

    // ── Skybox: procedural sky behind the terrain (same render pass, between opaque and transparent passes) ──
    // The skybox is depth-tested with eLessOrEqual at z=1, so it fills any pixel the opaque
    // pass left untouched (no depth write from skybox). Drawing it here means the transparent
    // pass below will alpha-blend over either the opaque colour or the skybox colour, which
    // is the visually correct order for "see through glass at the sky" cases.
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, skybox_pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, skybox_layout, 0, {skybox_descriptor_set}, {});
    cmd.pushConstants<float>(skybox_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, time);
    cmd.draw(3, 1, 0, 0);

    // ── Transparent pass: render entities [opaque_object_count, total_objects) ──
    // Premultiplied alpha blend over whatever the opaque pass + skybox wrote. Depth is
    // tested but not written, so multiple transparent surfaces stacked along the same
    // ray would composite in arbitrary draw order rather than back-to-front. The
    // current scene has only two transparent entities (glass tower + energy-barrier
    // pillar) at well-separated world positions that do not overlap from any
    // reasonable viewpoint, so CPU-side back-to-front sorting is unnecessary; it
    // would be needed if future content added closely-stacked translucent geometry.
    if (transparent_object_count > 0) {
        // Re-bind the mesh-pipeline descriptor set: the skybox draw above bound a different
        // descriptor set with skybox_pipeline_layout, which invalidates set 0 for any
        // subsequent draw using a non-compatible pipeline layout (skybox_pipeline_layout
        // vs pipeline.layout() are not compatible — different push-constant ranges and
        // different descriptor set layouts). Without this rebind the transparent dispatch
        // would read garbage descriptor data and validation would (correctly) flag a
        // pipeline-layout-compatibility error.
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.transparentPipeline());
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline.layout(), 0, {descriptor_set}, {});

        TaskPushConstants transparent_push{};
        transparent_push.planes = frustum.planes;
        transparent_push.object_count = transparent_object_count;
        transparent_push.base_object_index = opaque_object_count;
        cmd.pushConstants<TaskPushConstants>(*pipeline.layout(), vk::ShaderStageFlagBits::eTaskEXT, 0, transparent_push);

        cmd.drawMeshTasksEXT(transparent_object_count, 1, 1);
    }

    cmd.endRendering();

    // ── Post-process: compute dispatch HDR → swapchain ──

    // Transition HDR: COLOR_ATTACHMENT_OPTIMAL → GENERAL (compute shader storage read).
    // Transition swapchain: UNDEFINED → GENERAL (compute shader storage write).
    // Transition bloom: UNDEFINED → GENERAL (compute shader storage read/write).
    vk::ImageMemoryBarrier2 hdr_to_general{};
    hdr_to_general.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    hdr_to_general.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
    hdr_to_general.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    hdr_to_general.dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead;
    hdr_to_general.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
    hdr_to_general.newLayout = vk::ImageLayout::eGeneral;
    hdr_to_general.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    hdr_to_general.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    hdr_to_general.image = hdr_image;
    hdr_to_general.subresourceRange = colour_range;

    // srcStage = eColorAttachmentOutput to sync with the acquire semaphore wait.
    vk::ImageMemoryBarrier2 swap_to_general{};
    swap_to_general.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    swap_to_general.srcAccessMask = vk::AccessFlagBits2::eNone;
    swap_to_general.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    swap_to_general.dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
    swap_to_general.oldLayout = vk::ImageLayout::eUndefined;
    swap_to_general.newLayout = vk::ImageLayout::eGeneral;
    swap_to_general.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    swap_to_general.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    swap_to_general.image = swapchain_image;
    swap_to_general.subresourceRange = colour_range;

    vk::ImageMemoryBarrier2 bloom_to_general{};
    bloom_to_general.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    bloom_to_general.srcAccessMask = vk::AccessFlagBits2::eNone;
    bloom_to_general.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    bloom_to_general.dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
    bloom_to_general.oldLayout = vk::ImageLayout::eUndefined;
    bloom_to_general.newLayout = vk::ImageLayout::eGeneral;
    bloom_to_general.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    bloom_to_general.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    bloom_to_general.image = bloom_image;
    bloom_to_general.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    bloom_to_general.subresourceRange.baseMipLevel = 0;
    bloom_to_general.subresourceRange.levelCount = bloom_mip_count;
    bloom_to_general.subresourceRange.baseArrayLayer = 0;
    bloom_to_general.subresourceRange.layerCount = 1;

    // Froxel image: UNDEFINED (or previous-frame eGeneral) → eGeneral. Discarding the
    // prior frame's data is fine for 41a (no temporal reprojection yet); 41c will switch
    // this to a non-discarding eGeneral → eGeneral layout transition so the prior frame's
    // froxels stay readable for reprojection.
    vk::ImageMemoryBarrier2 froxel_to_general{};
    froxel_to_general.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    froxel_to_general.srcAccessMask = vk::AccessFlagBits2::eNone;
    froxel_to_general.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    froxel_to_general.dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
    froxel_to_general.oldLayout = vk::ImageLayout::eUndefined;
    froxel_to_general.newLayout = vk::ImageLayout::eGeneral;
    froxel_to_general.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    froxel_to_general.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    froxel_to_general.image = froxel_image;
    froxel_to_general.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    froxel_to_general.subresourceRange.baseMipLevel = 0;
    froxel_to_general.subresourceRange.levelCount = 1;
    froxel_to_general.subresourceRange.baseArrayLayer = 0;
    froxel_to_general.subresourceRange.layerCount = 1;

    std::array<vk::ImageMemoryBarrier2, 4> to_compute_barriers{hdr_to_general, swap_to_general, bloom_to_general, froxel_to_general};
    vk::DependencyInfo dep_to_compute{};
    dep_to_compute.setImageMemoryBarriers(to_compute_barriers);
    cmd.pipelineBarrier2(dep_to_compute);

    // ── Volumetric inject + composite (Phase 8 Etape 41 sub-etape 41a) ──
    // Inject writes the per-froxel scattered radiance + extinction; composite raymarches
    // along each pixel's view ray, accumulates the in-scattered radiance and transmittance,
    // and blends them onto the existing HDR colour. Sequenced before bloom extraction so
    // the bloom chain operates on the fogged HDR (the fog itself can bloom).

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, inject_pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, inject_pipeline_layout, 0, {inject_descriptor_set}, {});
    cmd.pushConstants<VolumetricInjectPushConstants>(inject_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, inject_push);

    constexpr uint32_t INJECT_GROUP_SIZE{8};
    uint32_t inject_groups_x{(FROXEL_WIDTH + INJECT_GROUP_SIZE - 1) / INJECT_GROUP_SIZE};
    uint32_t inject_groups_y{(FROXEL_HEIGHT + INJECT_GROUP_SIZE - 1) / INJECT_GROUP_SIZE};
    cmd.dispatch(inject_groups_x, inject_groups_y, FROXEL_DEPTH);

    // Barrier: froxel write (inject) → froxel read (composite). Compute → compute, same
    // image, same layout, just an execution + memory dependency between dispatches.
    vk::ImageMemoryBarrier2 froxel_inject_to_composite{};
    froxel_inject_to_composite.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    froxel_inject_to_composite.srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
    froxel_inject_to_composite.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    froxel_inject_to_composite.dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead;
    froxel_inject_to_composite.oldLayout = vk::ImageLayout::eGeneral;
    froxel_inject_to_composite.newLayout = vk::ImageLayout::eGeneral;
    froxel_inject_to_composite.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    froxel_inject_to_composite.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    froxel_inject_to_composite.image = froxel_image;
    froxel_inject_to_composite.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    froxel_inject_to_composite.subresourceRange.baseMipLevel = 0;
    froxel_inject_to_composite.subresourceRange.levelCount = 1;
    froxel_inject_to_composite.subresourceRange.baseArrayLayer = 0;
    froxel_inject_to_composite.subresourceRange.layerCount = 1;

    vk::DependencyInfo dep_inject_to_composite{};
    dep_inject_to_composite.setImageMemoryBarriers(froxel_inject_to_composite);
    cmd.pipelineBarrier2(dep_inject_to_composite);

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, composite_pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, composite_pipeline_layout, 0, {composite_descriptor_set}, {});

    VolumetricCompositePushConstants composite_push{};
    composite_push.screen_width = extent.width;
    composite_push.screen_height = extent.height;
    composite_push.froxel_width = FROXEL_WIDTH;
    composite_push.froxel_height = FROXEL_HEIGHT;
    composite_push.froxel_depth = FROXEL_DEPTH;
    cmd.pushConstants<VolumetricCompositePushConstants>(composite_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, composite_push);

    uint32_t composite_groups_x{(extent.width + PP_WORKGROUP_SIZE - 1) / PP_WORKGROUP_SIZE};
    uint32_t composite_groups_y{(extent.height + PP_WORKGROUP_SIZE - 1) / PP_WORKGROUP_SIZE};
    cmd.dispatch(composite_groups_x, composite_groups_y, 1);

    // Barrier: HDR write (composite) → HDR read (bloom extract). Without this the bloom
    // chain could read stale HDR (pre-fog) and the post-process pass would see bloom that
    // doesn't match the fogged scene.
    vk::ImageMemoryBarrier2 hdr_composite_to_bloom{};
    hdr_composite_to_bloom.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    hdr_composite_to_bloom.srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
    hdr_composite_to_bloom.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    hdr_composite_to_bloom.dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead;
    hdr_composite_to_bloom.oldLayout = vk::ImageLayout::eGeneral;
    hdr_composite_to_bloom.newLayout = vk::ImageLayout::eGeneral;
    hdr_composite_to_bloom.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    hdr_composite_to_bloom.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    hdr_composite_to_bloom.image = hdr_image;
    hdr_composite_to_bloom.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    hdr_composite_to_bloom.subresourceRange.baseMipLevel = 0;
    hdr_composite_to_bloom.subresourceRange.levelCount = 1;
    hdr_composite_to_bloom.subresourceRange.baseArrayLayer = 0;
    hdr_composite_to_bloom.subresourceRange.layerCount = 1;

    vk::DependencyInfo dep_hdr_composite_to_bloom{};
    dep_hdr_composite_to_bloom.setImageMemoryBarriers(hdr_composite_to_bloom);
    cmd.pipelineBarrier2(dep_hdr_composite_to_bloom);

    // ── Bloom extraction + downsample + upsample chain ──

    recordBloomDownsample(cmd, bloom_image, bloom_mip_count, bloom_base_w, bloom_base_h, bloom_extract_pipeline, bloom_downsample_pipeline, bloom_layout, bloom_sets,
        BLOOM_THRESHOLD);

    recordBloomUpsample(cmd, bloom_image, bloom_mip_count, bloom_base_w, bloom_base_h, bloom_upsample_pipeline, bloom_layout, bloom_upsample_sets);

    // Barrier: bloom upsample writes must complete before tonemap reads bloom mip 0.
    // Narrowed to mip 0 only (the only mip the post-process shader samples) so the
    // driver can pipeline other work against the remaining mip chain.
    vk::ImageMemoryBarrier2 bloom_after{};
    bloom_after.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    bloom_after.srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
    bloom_after.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    bloom_after.dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead;
    bloom_after.oldLayout = vk::ImageLayout::eGeneral;
    bloom_after.newLayout = vk::ImageLayout::eGeneral;
    bloom_after.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    bloom_after.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    bloom_after.image = bloom_image;
    bloom_after.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    bloom_after.subresourceRange.baseMipLevel = 0;
    bloom_after.subresourceRange.levelCount = 1;
    bloom_after.subresourceRange.baseArrayLayer = 0;
    bloom_after.subresourceRange.layerCount = 1;

    vk::DependencyInfo dep_bloom_done{};
    dep_bloom_done.setImageMemoryBarriers(bloom_after);
    cmd.pipelineBarrier2(dep_bloom_done);

    // ── Tonemap: HDR + bloom → swapchain ──

    // Bind compute pipeline, push bloom strength, and dispatch post-process.
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pp_pipeline);
    cmd.pushConstants<float>(pp_layout, vk::ShaderStageFlagBits::eCompute, 0, BLOOM_STRENGTH);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pp_layout, 0, {pp_descriptor_set}, {});

    uint32_t group_x{(extent.width + PP_WORKGROUP_SIZE - 1) / PP_WORKGROUP_SIZE};
    uint32_t group_y{(extent.height + PP_WORKGROUP_SIZE - 1) / PP_WORKGROUP_SIZE};
    cmd.dispatch(group_x, group_y, 1);

    // Transition swapchain: GENERAL → PRESENT_SRC_KHR.
    // dstStage uses eAllCommands to match the submit's signal-semaphore stage mask
    // (render_finished is signalled at eAllCommands) — eBottomOfPipe is deprecated
    // in sync2 and creates no memory-availability guarantee with the signal.
    vk::ImageMemoryBarrier2 swap_to_present{};
    swap_to_present.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    swap_to_present.srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
    swap_to_present.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
    swap_to_present.dstAccessMask = vk::AccessFlagBits2::eNone;
    swap_to_present.oldLayout = vk::ImageLayout::eGeneral;
    swap_to_present.newLayout = vk::ImageLayout::ePresentSrcKHR;
    swap_to_present.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    swap_to_present.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    swap_to_present.image = swapchain_image;
    swap_to_present.subresourceRange = colour_range;

    vk::DependencyInfo dep_to_present{};
    dep_to_present.setImageMemoryBarriers(swap_to_present);
    cmd.pipelineBarrier2(dep_to_present);

    cmd.end();
}

/*!
    Render thread entry point. Blocks on a condition variable until the main thread
    sends RenderEvent messages via Signal<T>. Owns the Vulkan rendering timeline,
    camera state, input processing, and delta time.
*/
static void renderThread(Device& device, Swapchain& swapchain, Pipeline& pipeline, Allocator& allocator, uint32_t opaque_object_count, uint32_t transparent_object_count,
    uint32_t emissive_count, float total_emissive_power, vk::raii::CommandPool& command_pool, vk::raii::CommandBuffers& command_buffers, VkBuffer reservoir_a_buf,
    VkBuffer reservoir_b_buf, VkDeviceSize reservoir_buffer_size, SignalsLib::Signal<RenderEvent>& render_signal, std::mutex& render_mutex,
    std::condition_variable& render_cv, LoggingLib::Logger& logger)
{
    // Frame synchronisation — per-frame fences and acquire semaphores
    std::vector<vk::raii::Semaphore> image_available_semaphores;
    std::vector<vk::raii::Fence> in_flight_fences;

    vk::SemaphoreCreateInfo sem_info{};
    vk::FenceCreateInfo fence_info{};
    fence_info.flags = vk::FenceCreateFlagBits::eSignaled;

    for (uint32_t i{0}; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        image_available_semaphores.push_back(vk::raii::Semaphore(device.get(), sem_info));
        in_flight_fences.push_back(vk::raii::Fence(device.get(), fence_info));
    }

    // Per-swapchain-image semaphores for present
    std::vector<vk::raii::Semaphore> render_finished_semaphores;

    // Rebuilds the present-side semaphores AND the per-frame acquire semaphores.
    // Also rebuilding image_available_semaphores guarantees they return to the
    // unsignalled state even if a prior acquire signalled the semaphore and then
    // the swapchain went out-of-date before the signal was consumed — vkDeviceWaitIdle
    // does NOT reset semaphore signal state, so without this rebuild the next
    // acquireNextImage with the same semaphore violates the spec.
    auto rebuildPresentSemaphores = [&]() {
        render_finished_semaphores.clear();
        for (uint32_t i{0}; i < swapchain.imageCount(); ++i) {
            render_finished_semaphores.push_back(vk::raii::Semaphore(device.get(), sem_info));
        }
        image_available_semaphores.clear();
        for (uint32_t i{0}; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            image_available_semaphores.push_back(vk::raii::Semaphore(device.get(), sem_info));
        }
    };
    rebuildPresentSemaphores();

    // MSAA colour buffer — multisampled, transient (only the resolved result is kept).
    VkSampleCountFlagBits msaa_samples{static_cast<VkSampleCountFlagBits>(device.maxMsaaSamples())};

    AllocatedImage msaa_image{allocator.createImage(swapchain.extent().width, swapchain.extent().height, static_cast<VkFormat>(HDR_FORMAT),
        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, msaa_samples)};

    vk::ImageViewCreateInfo msaa_view_info{};
    msaa_view_info.image = msaa_image.image();
    msaa_view_info.viewType = vk::ImageViewType::e2D;
    msaa_view_info.format = HDR_FORMAT;
    msaa_view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    msaa_view_info.subresourceRange.baseMipLevel = 0;
    msaa_view_info.subresourceRange.levelCount = 1;
    msaa_view_info.subresourceRange.baseArrayLayer = 0;
    msaa_view_info.subresourceRange.layerCount = 1;
    vk::raii::ImageView msaa_view(device.get(), msaa_view_info);

    // Depth buffer — multisampled to match MSAA colour, recreated on resize.
    AllocatedImage depth_image{allocator.createImage(swapchain.extent().width, swapchain.extent().height, static_cast<VkFormat>(DEPTH_FORMAT),
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, msaa_samples)};

    vk::ImageViewCreateInfo depth_view_info{};
    depth_view_info.image = depth_image.image();
    depth_view_info.viewType = vk::ImageViewType::e2D;
    depth_view_info.format = DEPTH_FORMAT;
    depth_view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
    depth_view_info.subresourceRange.baseMipLevel = 0;
    depth_view_info.subresourceRange.levelCount = 1;
    depth_view_info.subresourceRange.baseArrayLayer = 0;
    depth_view_info.subresourceRange.layerCount = 1;
    vk::raii::ImageView depth_view(device.get(), depth_view_info);

    // HDR colour buffer — single-sample resolve target, recreated on resize.
    // STORAGE_BIT allows the post-process compute shader to read the HDR image.
    AllocatedImage hdr_image{allocator.createImage(swapchain.extent().width, swapchain.extent().height, static_cast<VkFormat>(HDR_FORMAT),
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT)};

    vk::ImageViewCreateInfo hdr_view_info{};
    hdr_view_info.image = hdr_image.image();
    hdr_view_info.viewType = vk::ImageViewType::e2D;
    hdr_view_info.format = HDR_FORMAT;
    hdr_view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    hdr_view_info.subresourceRange.baseMipLevel = 0;
    hdr_view_info.subresourceRange.levelCount = 1;
    hdr_view_info.subresourceRange.baseArrayLayer = 0;
    hdr_view_info.subresourceRange.layerCount = 1;
    vk::raii::ImageView hdr_view(device.get(), hdr_view_info);

    // Per-swapchain-image storage views for compute shader write access.
    std::vector<vk::raii::ImageView> swapchain_storage_views;

    auto rebuildSwapchainStorageViews = [&]() {
        swapchain_storage_views.clear();
        for (uint32_t i{0}; i < swapchain.imageCount(); ++i) {
            vk::ImageViewCreateInfo view_info{};
            view_info.image = swapchain.images()[i];
            view_info.viewType = vk::ImageViewType::e2D;
            view_info.format = swapchain.format().format;
            view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;
            swapchain_storage_views.push_back(vk::raii::ImageView(device.get(), view_info));
        }
    };
    rebuildSwapchainStorageViews();

    // Bloom texture — half resolution with mip chain, recreated on resize.
    uint32_t bloom_mip_count{bloomMipCount(swapchain.extent())};
    uint32_t bloom_base_w{std::max(swapchain.extent().width / 2, 1u)};
    uint32_t bloom_base_h{std::max(swapchain.extent().height / 2, 1u)};

    AllocatedImage bloom_image{
        allocator.createImage(bloom_base_w, bloom_base_h, static_cast<VkFormat>(HDR_FORMAT), VK_IMAGE_USAGE_STORAGE_BIT, VK_SAMPLE_COUNT_1_BIT, bloom_mip_count)};

    // Per-mip views for compute shader access.
    std::vector<vk::raii::ImageView> bloom_mip_views;

    auto rebuildBloomMipViews = [&]() {
        bloom_mip_views.clear();
        for (uint32_t i{0}; i < bloom_mip_count; ++i) {
            vk::ImageViewCreateInfo view_info{};
            view_info.image = bloom_image.image();
            view_info.viewType = vk::ImageViewType::e2D;
            view_info.format = HDR_FORMAT;
            view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            view_info.subresourceRange.baseMipLevel = i;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;
            bloom_mip_views.push_back(vk::raii::ImageView(device.get(), view_info));
        }
    };
    rebuildBloomMipViews();

    // Capacity check + zero-fill of the reservoir ping-pong buffers. Called from
    // rebuildFrameBuffers so that each resize starts with a clean ReSTIR state —
    // without this, the one-frame-after-resize sees reservoir indices that now
    // map to different pixels than they did before (row stride changed), producing
    // a brief "poison" frame of smeared lighting until temporal decay kicks in.
    VkDeviceSize reservoir_pixel_capacity{reservoir_buffer_size / sizeof(Reservoir)};
    auto resetReservoirs = [&]() {
        VkDeviceSize required_pixels{static_cast<VkDeviceSize>(swapchain.extent().width) * swapchain.extent().height};
        if (required_pixels > reservoir_pixel_capacity) {
            logger.logFatal("Swapchain extent exceeds reservoir capacity (" + std::to_string(swapchain.extent().width) + "x" + std::to_string(swapchain.extent().height)
                + " pixels, capacity " + std::to_string(reservoir_pixel_capacity) + "). Resize beyond 4K is not supported.");
            std::abort();
            return;
        }

        // One-shot command buffer — reuses the existing command pool. Caller is
        // responsible for draining GPU work before calling this (swapchain.recreate
        // does waitIdle first), so it's safe to submit + waitIdle again here.
        vk::CommandBufferAllocateInfo cmd_alloc{};
        cmd_alloc.commandPool = *command_pool;
        cmd_alloc.level = vk::CommandBufferLevel::ePrimary;
        cmd_alloc.commandBufferCount = 1;
        vk::raii::CommandBuffers reset_cmds{device.get(), cmd_alloc};

        const vk::raii::CommandBuffer& reset_cmd{reset_cmds[0]};
        vk::CommandBufferBeginInfo begin_info{};
        begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        reset_cmd.begin(begin_info);
        reset_cmd.fillBuffer(reservoir_a_buf, 0, reservoir_buffer_size, 0);
        reset_cmd.fillBuffer(reservoir_b_buf, 0, reservoir_buffer_size, 0);
        reset_cmd.end();

        vk::CommandBuffer raw_cmd{*reset_cmd};
        vk::SubmitInfo submit{};
        submit.setCommandBuffers(raw_cmd);
        device.graphicsQueue().submit({submit});
        device.graphicsQueue().waitIdle();
    };

    // Rebuilds MSAA, depth, HDR, bloom, and swapchain storage views to match the current swapchain extent.
    auto rebuildFrameBuffers = [&]() {
        msaa_image = allocator.createImage(swapchain.extent().width, swapchain.extent().height, static_cast<VkFormat>(HDR_FORMAT),
            VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, msaa_samples);
        msaa_view_info.image = msaa_image.image();
        msaa_view = vk::raii::ImageView(device.get(), msaa_view_info);

        depth_image = allocator.createImage(swapchain.extent().width, swapchain.extent().height, static_cast<VkFormat>(DEPTH_FORMAT),
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, msaa_samples);
        depth_view_info.image = depth_image.image();
        depth_view = vk::raii::ImageView(device.get(), depth_view_info);

        hdr_image = allocator.createImage(swapchain.extent().width, swapchain.extent().height, static_cast<VkFormat>(HDR_FORMAT),
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
        hdr_view_info.image = hdr_image.image();
        hdr_view = vk::raii::ImageView(device.get(), hdr_view_info);

        bloom_mip_count = bloomMipCount(swapchain.extent());
        bloom_base_w = std::max(swapchain.extent().width / 2, 1u);
        bloom_base_h = std::max(swapchain.extent().height / 2, 1u);
        bloom_image = allocator.createImage(bloom_base_w, bloom_base_h, static_cast<VkFormat>(HDR_FORMAT), VK_IMAGE_USAGE_STORAGE_BIT, VK_SAMPLE_COUNT_1_BIT,
            bloom_mip_count);
        rebuildBloomMipViews();

        rebuildSwapchainStorageViews();

        // Reset the ReSTIR reservoirs — the row-stride changed with the extent so the
        // pre-resize pixel indices now map to different pixels.
        resetReservoirs();
    };

    // ── Post-process compute pipeline (inline — no class until second use case) ──

    std::string exe_dir_rt{executableDirectory()};
    std::vector<uint32_t> postprocess_spirv{loadSpirv(exe_dir_rt + "postprocess.spv", logger)};

    // Descriptor set layout: 3 storage images (HDR input + swapchain output + bloom).
    std::array<vk::DescriptorSetLayoutBinding, 3> pp_bindings{};
    pp_bindings[0].binding = 0;
    pp_bindings[0].descriptorType = vk::DescriptorType::eStorageImage;
    pp_bindings[0].descriptorCount = 1;
    pp_bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;
    pp_bindings[1].binding = 1;
    pp_bindings[1].descriptorType = vk::DescriptorType::eStorageImage;
    pp_bindings[1].descriptorCount = 1;
    pp_bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;
    pp_bindings[2].binding = 2;
    pp_bindings[2].descriptorType = vk::DescriptorType::eStorageImage;
    pp_bindings[2].descriptorCount = 1;
    pp_bindings[2].stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo pp_layout_info{};
    pp_layout_info.setBindings(pp_bindings);
    vk::raii::DescriptorSetLayout pp_descriptor_set_layout{device.get(), pp_layout_info};

    vk::PushConstantRange pp_push_range{};
    pp_push_range.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pp_push_range.offset = 0;
    pp_push_range.size = sizeof(float);

    vk::PipelineLayoutCreateInfo pp_pipeline_layout_info{};
    pp_pipeline_layout_info.setSetLayouts(*pp_descriptor_set_layout);
    pp_pipeline_layout_info.setPushConstantRanges(pp_push_range);
    vk::raii::PipelineLayout pp_pipeline_layout{device.get(), pp_pipeline_layout_info};

    vk::ShaderModuleCreateInfo pp_module_info{};
    pp_module_info.setCode(postprocess_spirv);
    vk::raii::ShaderModule pp_module{device.get(), pp_module_info};

    vk::PipelineShaderStageCreateInfo pp_stage{};
    pp_stage.stage = vk::ShaderStageFlagBits::eCompute;
    pp_stage.module = *pp_module;
    pp_stage.setPName("postprocessMain");

    vk::ComputePipelineCreateInfo pp_pipeline_info{};
    pp_pipeline_info.stage = pp_stage;
    pp_pipeline_info.layout = *pp_pipeline_layout;
    vk::raii::Pipeline pp_pipeline{device.get(), nullptr, pp_pipeline_info};

    logger.logInfo("Post-process compute pipeline created.");

    // Descriptor pool and sets for post-process.
    std::array<vk::DescriptorPoolSize, 1> pp_pool_sizes{};
    pp_pool_sizes[0].type = vk::DescriptorType::eStorageImage;
    pp_pool_sizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT * 3;

    vk::DescriptorPoolCreateInfo pp_pool_info{};
    pp_pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    pp_pool_info.maxSets = MAX_FRAMES_IN_FLIGHT;
    pp_pool_info.setPoolSizes(pp_pool_sizes);
    vk::raii::DescriptorPool pp_descriptor_pool{device.get(), pp_pool_info};

    std::vector<vk::DescriptorSetLayout> pp_layouts(MAX_FRAMES_IN_FLIGHT, *pp_descriptor_set_layout);
    vk::DescriptorSetAllocateInfo pp_alloc_info{};
    pp_alloc_info.descriptorPool = *pp_descriptor_pool;
    pp_alloc_info.setSetLayouts(pp_layouts);
    std::vector<vk::raii::DescriptorSet> pp_descriptor_sets{device.get().allocateDescriptorSets(pp_alloc_info)};

    // Updates post-process descriptor set bindings for the given frame.
    auto updatePostProcessDescriptors = [&](uint32_t frame_index, vk::ImageView hdr_sv, vk::ImageView swap_sv, vk::ImageView bloom_sv) {
        vk::DescriptorImageInfo hdr_info{};
        hdr_info.imageView = hdr_sv;
        hdr_info.imageLayout = vk::ImageLayout::eGeneral;

        vk::DescriptorImageInfo swap_info{};
        swap_info.imageView = swap_sv;
        swap_info.imageLayout = vk::ImageLayout::eGeneral;

        vk::DescriptorImageInfo bloom_info{};
        bloom_info.imageView = bloom_sv;
        bloom_info.imageLayout = vk::ImageLayout::eGeneral;

        std::array<vk::WriteDescriptorSet, 3> writes{};
        writes[0].dstSet = *pp_descriptor_sets[frame_index];
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = vk::DescriptorType::eStorageImage;
        writes[0].setImageInfo(hdr_info);

        writes[1].dstSet = *pp_descriptor_sets[frame_index];
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = vk::DescriptorType::eStorageImage;
        writes[1].setImageInfo(swap_info);

        writes[2].dstSet = *pp_descriptor_sets[frame_index];
        writes[2].dstBinding = 2;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorType = vk::DescriptorType::eStorageImage;
        writes[2].setImageInfo(bloom_info);

        device.get().updateDescriptorSets(writes, {});
    };

    // ── Bloom compute pipelines (extraction + downsample + upsample) ──

    std::vector<uint32_t> bloom_extract_spirv{loadSpirv(exe_dir_rt + "bloom_extract.spv", logger)};
    std::vector<uint32_t> bloom_down_spirv{loadSpirv(exe_dir_rt + "bloom_downsample.spv", logger)};

    // Descriptor set layout: 3 storage images (HDR input, bloom source mip, bloom dest mip).
    std::array<vk::DescriptorSetLayoutBinding, 3> bloom_bindings{};
    bloom_bindings[0].binding = 0;
    bloom_bindings[0].descriptorType = vk::DescriptorType::eStorageImage;
    bloom_bindings[0].descriptorCount = 1;
    bloom_bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;
    bloom_bindings[1].binding = 1;
    bloom_bindings[1].descriptorType = vk::DescriptorType::eStorageImage;
    bloom_bindings[1].descriptorCount = 1;
    bloom_bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;
    bloom_bindings[2].binding = 2;
    bloom_bindings[2].descriptorType = vk::DescriptorType::eStorageImage;
    bloom_bindings[2].descriptorCount = 1;
    bloom_bindings[2].stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo bloom_layout_info{};
    bloom_layout_info.setBindings(bloom_bindings);
    vk::raii::DescriptorSetLayout bloom_descriptor_set_layout{device.get(), bloom_layout_info};

    vk::PushConstantRange bloom_push_range{};
    bloom_push_range.stageFlags = vk::ShaderStageFlagBits::eCompute;
    bloom_push_range.offset = 0;
    bloom_push_range.size = sizeof(float);

    vk::PipelineLayoutCreateInfo bloom_pipeline_layout_info{};
    bloom_pipeline_layout_info.setSetLayouts(*bloom_descriptor_set_layout);
    bloom_pipeline_layout_info.setPushConstantRanges(bloom_push_range);
    vk::raii::PipelineLayout bloom_pipeline_layout{device.get(), bloom_pipeline_layout_info};

    // Bloom extraction pipeline.
    vk::ShaderModuleCreateInfo bloom_extract_module_info{};
    bloom_extract_module_info.setCode(bloom_extract_spirv);
    vk::raii::ShaderModule bloom_extract_module{device.get(), bloom_extract_module_info};

    vk::PipelineShaderStageCreateInfo bloom_extract_stage{};
    bloom_extract_stage.stage = vk::ShaderStageFlagBits::eCompute;
    bloom_extract_stage.module = *bloom_extract_module;
    bloom_extract_stage.setPName("bloomExtractMain");

    vk::ComputePipelineCreateInfo bloom_extract_pipeline_info{};
    bloom_extract_pipeline_info.stage = bloom_extract_stage;
    bloom_extract_pipeline_info.layout = *bloom_pipeline_layout;
    vk::raii::Pipeline bloom_extract_pipeline{device.get(), nullptr, bloom_extract_pipeline_info};

    // Bloom downsample pipeline.
    vk::ShaderModuleCreateInfo bloom_down_module_info{};
    bloom_down_module_info.setCode(bloom_down_spirv);
    vk::raii::ShaderModule bloom_down_module{device.get(), bloom_down_module_info};

    vk::PipelineShaderStageCreateInfo bloom_down_stage{};
    bloom_down_stage.stage = vk::ShaderStageFlagBits::eCompute;
    bloom_down_stage.module = *bloom_down_module;
    bloom_down_stage.setPName("bloomDownsampleMain");

    vk::ComputePipelineCreateInfo bloom_down_pipeline_info{};
    bloom_down_pipeline_info.stage = bloom_down_stage;
    bloom_down_pipeline_info.layout = *bloom_pipeline_layout;
    vk::raii::Pipeline bloom_downsample_pipeline{device.get(), nullptr, bloom_down_pipeline_info};

    // Bloom upsample pipeline.
    std::vector<uint32_t> bloom_up_spirv{loadSpirv(exe_dir_rt + "bloom_upsample.spv", logger)};

    vk::ShaderModuleCreateInfo bloom_up_module_info{};
    bloom_up_module_info.setCode(bloom_up_spirv);
    vk::raii::ShaderModule bloom_up_module{device.get(), bloom_up_module_info};

    vk::PipelineShaderStageCreateInfo bloom_up_stage{};
    bloom_up_stage.stage = vk::ShaderStageFlagBits::eCompute;
    bloom_up_stage.module = *bloom_up_module;
    bloom_up_stage.setPName("bloomUpsampleMain");

    vk::ComputePipelineCreateInfo bloom_up_pipeline_info{};
    bloom_up_pipeline_info.stage = bloom_up_stage;
    bloom_up_pipeline_info.layout = *bloom_pipeline_layout;
    vk::raii::Pipeline bloom_upsample_pipeline{device.get(), nullptr, bloom_up_pipeline_info};

    logger.logInfo("Bloom compute pipelines created (extract + downsample + upsample).");

    // Descriptor pool and sets for bloom — downsample + upsample sets per mip per frame.
    uint32_t bloom_total_sets{MAX_FRAMES_IN_FLIGHT * BLOOM_MAX_MIPS * 2};

    std::array<vk::DescriptorPoolSize, 1> bloom_pool_sizes{};
    bloom_pool_sizes[0].type = vk::DescriptorType::eStorageImage;
    bloom_pool_sizes[0].descriptorCount = bloom_total_sets * 3;

    vk::DescriptorPoolCreateInfo bloom_pool_info{};
    bloom_pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    bloom_pool_info.maxSets = bloom_total_sets;
    bloom_pool_info.setPoolSizes(bloom_pool_sizes);
    vk::raii::DescriptorPool bloom_descriptor_pool{device.get(), bloom_pool_info};

    uint32_t bloom_half_sets{MAX_FRAMES_IN_FLIGHT * BLOOM_MAX_MIPS};

    std::vector<vk::DescriptorSetLayout> bloom_down_layouts(bloom_half_sets, *bloom_descriptor_set_layout);
    vk::DescriptorSetAllocateInfo bloom_alloc_info{};
    bloom_alloc_info.descriptorPool = *bloom_descriptor_pool;
    bloom_alloc_info.setSetLayouts(bloom_down_layouts);
    std::vector<vk::raii::DescriptorSet> bloom_descriptor_sets{device.get().allocateDescriptorSets(bloom_alloc_info)};

    std::vector<vk::DescriptorSetLayout> bloom_up_layouts(bloom_half_sets, *bloom_descriptor_set_layout);
    vk::DescriptorSetAllocateInfo bloom_up_alloc_info{};
    bloom_up_alloc_info.descriptorPool = *bloom_descriptor_pool;
    bloom_up_alloc_info.setSetLayouts(bloom_up_layouts);
    std::vector<vk::raii::DescriptorSet> bloom_upsample_descriptor_sets{device.get().allocateDescriptorSets(bloom_up_alloc_info)};

    // Updates bloom downsample descriptor set bindings for the given frame.
    auto updateBloomDescriptors = [&](uint32_t frame_index) {
        for (uint32_t i{0}; i < bloom_mip_count; ++i) {
            uint32_t set_idx{frame_index * BLOOM_MAX_MIPS + i};

            std::array<vk::WriteDescriptorSet, 3> writes{};
            std::array<vk::DescriptorImageInfo, 3> infos{};

            // Binding 0: HDR input (used by extraction at step 0).
            infos[0].imageView = *hdr_view;
            infos[0].imageLayout = vk::ImageLayout::eGeneral;
            writes[0].dstSet = *bloom_descriptor_sets[set_idx];
            writes[0].dstBinding = 0;
            writes[0].dstArrayElement = 0;
            writes[0].descriptorType = vk::DescriptorType::eStorageImage;
            writes[0].setImageInfo(infos[0]);

            // Binding 1: bloom source mip (step 0 doesn't use this, but must bind something valid).
            vk::ImageView src_view{(i > 0) ? *bloom_mip_views[i - 1] : *bloom_mip_views[0]};
            infos[1].imageView = src_view;
            infos[1].imageLayout = vk::ImageLayout::eGeneral;
            writes[1].dstSet = *bloom_descriptor_sets[set_idx];
            writes[1].dstBinding = 1;
            writes[1].dstArrayElement = 0;
            writes[1].descriptorType = vk::DescriptorType::eStorageImage;
            writes[1].setImageInfo(infos[1]);

            // Binding 2: bloom destination mip.
            infos[2].imageView = *bloom_mip_views[i];
            infos[2].imageLayout = vk::ImageLayout::eGeneral;
            writes[2].dstSet = *bloom_descriptor_sets[set_idx];
            writes[2].dstBinding = 2;
            writes[2].dstArrayElement = 0;
            writes[2].descriptorType = vk::DescriptorType::eStorageImage;
            writes[2].setImageInfo(infos[2]);

            device.get().updateDescriptorSets(writes, {});
        }
    };

    // Updates bloom upsample descriptor set bindings for the given frame.
    auto updateBloomUpsampleDescriptors = [&](uint32_t frame_index) {
        for (uint32_t i{0}; i < bloom_mip_count; ++i) {
            uint32_t set_idx{frame_index * BLOOM_MAX_MIPS + i};

            std::array<vk::WriteDescriptorSet, 3> writes{};
            std::array<vk::DescriptorImageInfo, 3> infos{};

            // Binding 0: HDR input (unused by upsample, but must bind something valid).
            infos[0].imageView = *hdr_view;
            infos[0].imageLayout = vk::ImageLayout::eGeneral;
            writes[0].dstSet = *bloom_upsample_descriptor_sets[set_idx];
            writes[0].dstBinding = 0;
            writes[0].dstArrayElement = 0;
            writes[0].descriptorType = vk::DescriptorType::eStorageImage;
            writes[0].setImageInfo(infos[0]);

            // Binding 1: source mip (i + 1, smaller). Last mip has no source — bind self.
            vk::ImageView src_view{(i + 1 < bloom_mip_count) ? *bloom_mip_views[i + 1] : *bloom_mip_views[i]};
            infos[1].imageView = src_view;
            infos[1].imageLayout = vk::ImageLayout::eGeneral;
            writes[1].dstSet = *bloom_upsample_descriptor_sets[set_idx];
            writes[1].dstBinding = 1;
            writes[1].dstArrayElement = 0;
            writes[1].descriptorType = vk::DescriptorType::eStorageImage;
            writes[1].setImageInfo(infos[1]);

            // Binding 2: destination mip (i, larger).
            infos[2].imageView = *bloom_mip_views[i];
            infos[2].imageLayout = vk::ImageLayout::eGeneral;
            writes[2].dstSet = *bloom_upsample_descriptor_sets[set_idx];
            writes[2].dstBinding = 2;
            writes[2].dstArrayElement = 0;
            writes[2].descriptorType = vk::DescriptorType::eStorageImage;
            writes[2].setImageInfo(infos[2]);

            device.get().updateDescriptorSets(writes, {});
        }
    };

    // ── Volumetric fog (Phase 8 Etape 41 sub-etape 41a) — froxel grid + 2 compute pipelines ──

    // 3D froxel grid: stores per-froxel scattered radiance (rgb) + extinction (a).
    AllocatedImage froxel_image{allocator.createImage3D(FROXEL_WIDTH, FROXEL_HEIGHT, FROXEL_DEPTH, static_cast<VkFormat>(HDR_FORMAT), VK_IMAGE_USAGE_STORAGE_BIT)};

    vk::ImageViewCreateInfo froxel_view_info{};
    froxel_view_info.image = froxel_image.image();
    froxel_view_info.viewType = vk::ImageViewType::e3D;
    froxel_view_info.format = HDR_FORMAT;
    froxel_view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    froxel_view_info.subresourceRange.baseMipLevel = 0;
    froxel_view_info.subresourceRange.levelCount = 1;
    froxel_view_info.subresourceRange.baseArrayLayer = 0;
    froxel_view_info.subresourceRange.layerCount = 1;
    vk::raii::ImageView froxel_view{device.get(), froxel_view_info};

    // Inject pipeline — 1 storage image (the froxel grid) + push constants.
    std::vector<uint32_t> inject_spirv{loadSpirv(exe_dir_rt + "inject_density.spv", logger)};

    vk::DescriptorSetLayoutBinding inject_binding{};
    inject_binding.binding = 0;
    inject_binding.descriptorType = vk::DescriptorType::eStorageImage;
    inject_binding.descriptorCount = 1;
    inject_binding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo inject_layout_info{};
    inject_layout_info.setBindings(inject_binding);
    vk::raii::DescriptorSetLayout inject_descriptor_set_layout{device.get(), inject_layout_info};

    vk::PushConstantRange inject_push_range{};
    inject_push_range.stageFlags = vk::ShaderStageFlagBits::eCompute;
    inject_push_range.offset = 0;
    inject_push_range.size = sizeof(VolumetricInjectPushConstants);

    vk::PipelineLayoutCreateInfo inject_pipeline_layout_info{};
    inject_pipeline_layout_info.setSetLayouts(*inject_descriptor_set_layout);
    inject_pipeline_layout_info.setPushConstantRanges(inject_push_range);
    vk::raii::PipelineLayout inject_pipeline_layout{device.get(), inject_pipeline_layout_info};

    vk::ShaderModuleCreateInfo inject_module_info{};
    inject_module_info.setCode(inject_spirv);
    vk::raii::ShaderModule inject_module{device.get(), inject_module_info};

    vk::PipelineShaderStageCreateInfo inject_stage{};
    inject_stage.stage = vk::ShaderStageFlagBits::eCompute;
    inject_stage.module = *inject_module;
    inject_stage.setPName("injectMain");

    vk::ComputePipelineCreateInfo inject_pipeline_info{};
    inject_pipeline_info.stage = inject_stage;
    inject_pipeline_info.layout = *inject_pipeline_layout;
    vk::raii::Pipeline inject_pipeline{device.get(), nullptr, inject_pipeline_info};

    // Composite pipeline — 2 storage images (HDR in/out + froxel read) + push constants.
    std::vector<uint32_t> composite_spirv{loadSpirv(exe_dir_rt + "volumetric_composite.spv", logger)};

    std::array<vk::DescriptorSetLayoutBinding, 2> composite_bindings{};
    composite_bindings[0].binding = 0;
    composite_bindings[0].descriptorType = vk::DescriptorType::eStorageImage;
    composite_bindings[0].descriptorCount = 1;
    composite_bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;
    composite_bindings[1].binding = 1;
    composite_bindings[1].descriptorType = vk::DescriptorType::eStorageImage;
    composite_bindings[1].descriptorCount = 1;
    composite_bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo composite_layout_info{};
    composite_layout_info.setBindings(composite_bindings);
    vk::raii::DescriptorSetLayout composite_descriptor_set_layout{device.get(), composite_layout_info};

    vk::PushConstantRange composite_push_range{};
    composite_push_range.stageFlags = vk::ShaderStageFlagBits::eCompute;
    composite_push_range.offset = 0;
    composite_push_range.size = sizeof(VolumetricCompositePushConstants);

    vk::PipelineLayoutCreateInfo composite_pipeline_layout_info{};
    composite_pipeline_layout_info.setSetLayouts(*composite_descriptor_set_layout);
    composite_pipeline_layout_info.setPushConstantRanges(composite_push_range);
    vk::raii::PipelineLayout composite_pipeline_layout{device.get(), composite_pipeline_layout_info};

    vk::ShaderModuleCreateInfo composite_module_info{};
    composite_module_info.setCode(composite_spirv);
    vk::raii::ShaderModule composite_module{device.get(), composite_module_info};

    vk::PipelineShaderStageCreateInfo composite_stage{};
    composite_stage.stage = vk::ShaderStageFlagBits::eCompute;
    composite_stage.module = *composite_module;
    composite_stage.setPName("compositeMain");

    vk::ComputePipelineCreateInfo composite_pipeline_info{};
    composite_pipeline_info.stage = composite_stage;
    composite_pipeline_info.layout = *composite_pipeline_layout;
    vk::raii::Pipeline composite_pipeline{device.get(), nullptr, composite_pipeline_info};

    logger.logInfo("Volumetric compute pipelines created (inject + composite).");

    // Descriptor pool — 1 inject set (1 storage image) + MAX_FRAMES_IN_FLIGHT composite
    // sets (2 storage images each). Per-frame composite sets are required because the set
    // is updated each frame (HDR view binding) while the prior frame's command buffer may
    // still reference it; updating a set in use without VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
    // is a Vulkan validation error.
    std::array<vk::DescriptorPoolSize, 1> volumetric_pool_sizes{};
    volumetric_pool_sizes[0].type = vk::DescriptorType::eStorageImage;
    volumetric_pool_sizes[0].descriptorCount = 1 + (MAX_FRAMES_IN_FLIGHT * 2); // inject (1) + composite (2 per frame)

    vk::DescriptorPoolCreateInfo volumetric_pool_info{};
    volumetric_pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    volumetric_pool_info.maxSets = 1 + MAX_FRAMES_IN_FLIGHT;
    volumetric_pool_info.setPoolSizes(volumetric_pool_sizes);
    vk::raii::DescriptorPool volumetric_descriptor_pool{device.get(), volumetric_pool_info};

    std::array<vk::DescriptorSetLayout, 1> inject_set_layouts{*inject_descriptor_set_layout};
    vk::DescriptorSetAllocateInfo inject_alloc_info{};
    inject_alloc_info.descriptorPool = *volumetric_descriptor_pool;
    inject_alloc_info.setSetLayouts(inject_set_layouts);
    std::vector<vk::raii::DescriptorSet> inject_descriptor_sets{device.get().allocateDescriptorSets(inject_alloc_info)};

    std::vector<vk::DescriptorSetLayout> composite_set_layouts(MAX_FRAMES_IN_FLIGHT, *composite_descriptor_set_layout);
    vk::DescriptorSetAllocateInfo composite_alloc_info{};
    composite_alloc_info.descriptorPool = *volumetric_descriptor_pool;
    composite_alloc_info.setSetLayouts(composite_set_layouts);
    std::vector<vk::raii::DescriptorSet> composite_descriptor_sets{device.get().allocateDescriptorSets(composite_alloc_info)};

    // Inject descriptor set is bound once — only references the (persistent) froxel image.
    // No per-frame update needed; the same set is safely referenced by every command buffer.
    {
        vk::DescriptorImageInfo inject_image_info{};
        inject_image_info.imageView = *froxel_view;
        inject_image_info.imageLayout = vk::ImageLayout::eGeneral;

        vk::WriteDescriptorSet inject_write{};
        inject_write.dstSet = *inject_descriptor_sets[0];
        inject_write.dstBinding = 0;
        inject_write.dstArrayElement = 0;
        inject_write.descriptorType = vk::DescriptorType::eStorageImage;
        inject_write.setImageInfo(inject_image_info);

        device.get().updateDescriptorSets({inject_write}, {});
    }

    // Composite descriptor set is rebound each frame for the per-frame composite_descriptor_sets[frame_index]
    // — the HDR view changes on resize. Per-frame sets avoid the in-flight-update race.
    auto updateVolumetricCompositeDescriptors = [&](uint32_t frame_index, vk::ImageView hdr_sv) {
        std::array<vk::DescriptorImageInfo, 2> infos{};
        infos[0].imageView = hdr_sv;
        infos[0].imageLayout = vk::ImageLayout::eGeneral;
        infos[1].imageView = *froxel_view;
        infos[1].imageLayout = vk::ImageLayout::eGeneral;

        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0].dstSet = *composite_descriptor_sets[frame_index];
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = vk::DescriptorType::eStorageImage;
        writes[0].setImageInfo(infos[0]);
        writes[1].dstSet = *composite_descriptor_sets[frame_index];
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = vk::DescriptorType::eStorageImage;
        writes[1].setImageInfo(infos[1]);

        device.get().updateDescriptorSets(writes, {});
    };

    // ── Skybox graphics pipeline ──

    std::vector<uint32_t> skybox_spirv{loadSpirv(exe_dir_rt + "skybox.spv", logger)};

    vk::ShaderModuleCreateInfo skybox_module_info{};
    skybox_module_info.setCode(skybox_spirv);
    vk::raii::ShaderModule skybox_module{device.get(), skybox_module_info};

    std::array<vk::PipelineShaderStageCreateInfo, 2> skybox_stages{};
    skybox_stages[0].stage = vk::ShaderStageFlagBits::eVertex;
    skybox_stages[0].module = *skybox_module;
    skybox_stages[0].setPName("skyVertMain");
    skybox_stages[1].stage = vk::ShaderStageFlagBits::eFragment;
    skybox_stages[1].module = *skybox_module;
    skybox_stages[1].setPName("skyFragMain");

    vk::PipelineVertexInputStateCreateInfo skybox_vertex_input{};
    vk::PipelineInputAssemblyStateCreateInfo skybox_input_assembly{};
    skybox_input_assembly.topology = vk::PrimitiveTopology::eTriangleList;

    std::array<vk::DynamicState, 2> skybox_dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo skybox_dynamic_state{};
    skybox_dynamic_state.setDynamicStates(skybox_dynamic_states);

    vk::PipelineViewportStateCreateInfo skybox_viewport_state{};
    skybox_viewport_state.viewportCount = 1;
    skybox_viewport_state.scissorCount = 1;

    vk::PipelineRasterizationStateCreateInfo skybox_rasterisation{};
    skybox_rasterisation.polygonMode = vk::PolygonMode::eFill;
    skybox_rasterisation.cullMode = vk::CullModeFlagBits::eNone;
    skybox_rasterisation.frontFace = vk::FrontFace::eCounterClockwise;
    skybox_rasterisation.lineWidth = 1.0f;

    vk::PipelineDepthStencilStateCreateInfo skybox_depth_stencil{};
    skybox_depth_stencil.depthTestEnable = vk::True;
    skybox_depth_stencil.depthWriteEnable = vk::False;
    skybox_depth_stencil.depthCompareOp = vk::CompareOp::eLessOrEqual;

    vk::PipelineMultisampleStateCreateInfo skybox_multisample{};
    skybox_multisample.rasterizationSamples = device.maxMsaaSamples();
    skybox_multisample.sampleShadingEnable = vk::False;

    vk::PipelineColorBlendAttachmentState skybox_blend_attachment{};
    skybox_blend_attachment.blendEnable = vk::False;
    skybox_blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB
        | vk::ColorComponentFlagBits::eA;
    vk::PipelineColorBlendStateCreateInfo skybox_colour_blend{};
    skybox_colour_blend.setAttachments(skybox_blend_attachment);

    // Skybox descriptor set layout: 1 UBO for camera data.
    vk::DescriptorSetLayoutBinding skybox_ubo_binding{};
    skybox_ubo_binding.binding = 0;
    skybox_ubo_binding.descriptorType = vk::DescriptorType::eUniformBuffer;
    skybox_ubo_binding.descriptorCount = 1;
    skybox_ubo_binding.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo skybox_layout_info{};
    skybox_layout_info.setBindings(skybox_ubo_binding);
    vk::raii::DescriptorSetLayout skybox_descriptor_set_layout{device.get(), skybox_layout_info};

    vk::PushConstantRange skybox_push_range{};
    skybox_push_range.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    skybox_push_range.offset = 0;
    skybox_push_range.size = sizeof(float);

    vk::PipelineLayoutCreateInfo skybox_pipeline_layout_info{};
    skybox_pipeline_layout_info.setSetLayouts(*skybox_descriptor_set_layout);
    skybox_pipeline_layout_info.setPushConstantRanges(skybox_push_range);
    vk::raii::PipelineLayout skybox_pipeline_layout{device.get(), skybox_pipeline_layout_info};

    vk::PipelineRenderingCreateInfo skybox_rendering_info{};
    skybox_rendering_info.setColorAttachmentFormats(HDR_FORMAT);
    skybox_rendering_info.depthAttachmentFormat = DEPTH_FORMAT;

    vk::GraphicsPipelineCreateInfo skybox_pipeline_info{};
    skybox_pipeline_info.setPNext(&skybox_rendering_info);
    skybox_pipeline_info.setStages(skybox_stages);
    skybox_pipeline_info.setPVertexInputState(&skybox_vertex_input);
    skybox_pipeline_info.setPInputAssemblyState(&skybox_input_assembly);
    skybox_pipeline_info.setPViewportState(&skybox_viewport_state);
    skybox_pipeline_info.setPRasterizationState(&skybox_rasterisation);
    skybox_pipeline_info.setPMultisampleState(&skybox_multisample);
    skybox_pipeline_info.setPDepthStencilState(&skybox_depth_stencil);
    skybox_pipeline_info.setPColorBlendState(&skybox_colour_blend);
    skybox_pipeline_info.setPDynamicState(&skybox_dynamic_state);
    skybox_pipeline_info.layout = *skybox_pipeline_layout;

    vk::raii::Pipeline skybox_pipeline{device.get(), nullptr, skybox_pipeline_info};

    logger.logInfo("Skybox graphics pipeline created.");

    // Skybox descriptor pool and sets (1 UBO per frame).
    std::array<vk::DescriptorPoolSize, 1> skybox_pool_sizes{};
    skybox_pool_sizes[0].type = vk::DescriptorType::eUniformBuffer;
    skybox_pool_sizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;

    vk::DescriptorPoolCreateInfo skybox_pool_info{};
    skybox_pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    skybox_pool_info.maxSets = MAX_FRAMES_IN_FLIGHT;
    skybox_pool_info.setPoolSizes(skybox_pool_sizes);
    vk::raii::DescriptorPool skybox_descriptor_pool{device.get(), skybox_pool_info};

    std::vector<vk::DescriptorSetLayout> skybox_layouts(MAX_FRAMES_IN_FLIGHT, *skybox_descriptor_set_layout);
    vk::DescriptorSetAllocateInfo skybox_alloc_info{};
    skybox_alloc_info.descriptorPool = *skybox_descriptor_pool;
    skybox_alloc_info.setSetLayouts(skybox_layouts);
    std::vector<vk::raii::DescriptorSet> skybox_descriptor_sets{device.get().allocateDescriptorSets(skybox_alloc_info)};

    // Per-frame camera UBOs (persistently mapped)
    std::vector<AllocatedBuffer> ubo_buffers;
    for (uint32_t i{0}; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        AllocatedBuffer ubo{allocator.createBuffer(sizeof(CameraUBO), static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eUniformBuffer),
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO)};
        pipeline.bindUBO(i, ubo.buffer());
        VmaAllocationInfo ubo_info{ubo.allocationInfo()};
        pipeline.setUBOMappedPtr(i, ubo_info.pMappedData);
        // Bind the same UBO to the skybox descriptor set.
        vk::DescriptorBufferInfo skybox_ubo_info{};
        skybox_ubo_info.buffer = ubo.buffer();
        skybox_ubo_info.offset = 0;
        skybox_ubo_info.range = sizeof(CameraUBO);
        vk::WriteDescriptorSet skybox_ubo_write{};
        skybox_ubo_write.dstSet = *skybox_descriptor_sets[i];
        skybox_ubo_write.dstBinding = 0;
        skybox_ubo_write.dstArrayElement = 0;
        skybox_ubo_write.descriptorType = vk::DescriptorType::eUniformBuffer;
        skybox_ubo_write.setBufferInfo(skybox_ubo_info);
        device.get().updateDescriptorSets({skybox_ubo_write}, {});

        ubo_buffers.push_back(std::move(ubo));
    }

    // Camera starts above the terrain centre, looking along it.
    Camera camera({0.0f, 8.0f, 35.0f}, MathLib::PI / 4.0f, 0.1f, 200.0f);

    // Input state
    std::unordered_set<uint32_t> keys_held;
    bool mouse_captured{false};

    // Timing — delta time for camera movement, elapsed time for skybox animation.
    std::chrono::steady_clock::time_point start_time{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point last_time{start_time};

    uint32_t current_frame{0};
    uint32_t frame_counter{0};
    // Initialise to identity so frame 0's motion-vector reprojection produces finite values —
    // default Mat4{} is all zeros, which yields 0/0 = NaN in the shader's perspective divide.
    MathLib::Mat4 prev_view_projection{MathLib::Mat4::identity()};

    // Pre-allocated scratch vector for bloom descriptor sets (avoids per-frame heap allocation).
    std::vector<vk::DescriptorSet> bloom_sets_for_frame;
    bloom_sets_for_frame.reserve(BLOOM_MAX_MIPS);
    std::vector<vk::DescriptorSet> bloom_upsample_sets_for_frame;
    bloom_upsample_sets_for_frame.reserve(BLOOM_MAX_MIPS);

    while (true) {
        // Block until the main thread sends an event
        {
            std::unique_lock<std::mutex> lock(render_mutex);
            render_cv.wait(lock, [&render_signal, &keys_held]() {
                // Wake if there are events OR if keys are held (continuous movement)
                return !render_signal.empty() || !keys_held.empty();
            });
        }

        // Drain all pending events
        bool should_render{false};
        bool should_stop{false};
        bool needs_resize{false};
        uint32_t resize_width{0};
        uint32_t resize_height{0};

        RenderEvent ev;
        while (render_signal.consume(ev)) {
            switch (ev.type) {
            case RenderEvent::Type::Render:
                should_render = true;
                break;
            case RenderEvent::Type::Resize:
                needs_resize = true;
                should_render = true;
                resize_width = ev.width;
                resize_height = ev.height;
                break;
            case RenderEvent::Type::KeyDown:
                keys_held.insert(ev.width); // width field carries keycode
                should_render = true;
                break;
            case RenderEvent::Type::KeyUp:
                keys_held.erase(ev.width);
                should_render = true;
                break;
            case RenderEvent::Type::MouseMove:
                if (mouse_captured) {
                    camera.rotate(static_cast<float>(-ev.dx) * MOUSE_SENSITIVITY, static_cast<float>(ev.dy) * MOUSE_SENSITIVITY);
                    should_render = true;
                }
                break;
            case RenderEvent::Type::MouseButton:
                if ((ev.width == MOUSE_RIGHT) && ev.pressed) {
                    mouse_captured = !mouse_captured;
                    should_render = true;
                }
                break;
            case RenderEvent::Type::Stop:
                should_stop = true;
                keys_held.clear(); // Break self-notification loop — no more renders after Stop.
                break;
            }
        }

        if (should_stop) {
            // Wait for all in-flight fences + present queue before returning.
            // The render thread owns Vulkan objects (pipelines, semaphores, descriptors)
            // that are destroyed when this function exits — submitted GPU work and pending
            // presents must complete first.
            for (uint32_t i{0}; i < MAX_FRAMES_IN_FLIGHT; ++i) {
                while (device.get().waitForFences({*in_flight_fences[i]}, vk::True, FENCE_TIMEOUT_NS) == vk::Result::eTimeout) {
                }
            }
            device.presentQueue().waitIdle();
            return;
        }

        // Delta time
        std::chrono::steady_clock::time_point now{std::chrono::steady_clock::now()};
        float delta_time{std::chrono::duration<float>(now - last_time).count()};
        last_time = now;

        // Clamp delta time to avoid huge jumps (e.g., after breakpoint or resize stall)
        if (delta_time > 0.1f) {
            delta_time = 0.1f;
        }

        // Process held keys — camera movement
        if (!keys_held.empty()) {
            float move_delta{CAMERA_SPEED * delta_time};
            if (keys_held.contains(KEY_W)) {
                camera.moveForward(move_delta);
            }
            if (keys_held.contains(KEY_S)) {
                camera.moveForward(-move_delta);
            }
            if (keys_held.contains(KEY_A)) {
                camera.moveRight(-move_delta);
            }
            if (keys_held.contains(KEY_D)) {
                camera.moveRight(move_delta);
            }
            if (keys_held.contains(KEY_SPACE)) {
                camera.moveUp(move_delta);
            }
            if (keys_held.contains(KEY_SHIFT)) {
                camera.moveUp(-move_delta);
            }
            should_render = true;
        }

        // Handle swapchain recreation — skip if minimised (0x0)
        if (needs_resize) {
            if ((resize_width > 0) && (resize_height > 0)) {
                swapchain.recreate(resize_width, resize_height);
                rebuildPresentSemaphores();
                rebuildFrameBuffers();
            } else {
                should_render = false;
            }
        }

        // Skip rendering while minimised or if no render was requested
        if ((!should_render) || (swapchain.extent().width == 0) || (swapchain.extent().height == 0)) {
            // If keys are held, request another frame via self-notification
            if (!keys_held.empty()) {
                render_cv.notify_one();
            }
            continue;
        }

        // Wait for this frame's fence — poll with timeout to remain responsive to Stop events.
        // GPU-assisted validation can make frame submission very slow, so an infinite wait
        // would block the render thread from processing close events.
        while (true) {
            vk::Result wait_result{device.get().waitForFences({*in_flight_fences[current_frame]}, vk::True, FENCE_TIMEOUT_NS)};
            if (wait_result == vk::Result::eSuccess) {
                break;
            }
            if (wait_result == vk::Result::eTimeout) {
                // Check for Stop event while waiting for GPU.
                RenderEvent stop_check{};
                while (render_signal.consume(stop_check)) {
                    if (stop_check.type == RenderEvent::Type::Stop) {
                        // Wait for all in-flight fences + present queue (same as normal Stop).
                        for (uint32_t f{0}; f < MAX_FRAMES_IN_FLIGHT; ++f) {
                            while (device.get().waitForFences({*in_flight_fences[f]}, vk::True, FENCE_TIMEOUT_NS) == vk::Result::eTimeout) {
                            }
                        }
                        device.presentQueue().waitIdle();
                        return;
                    }
                }
                continue; // Retry fence wait.
            }
            logger.logFatal("Failed to wait for fence.");
            std::abort();
            return;
        }

        // Skip acquire when the swapchain is zero-extent (minimised). Previously we
        // acquired and then "drained" with waitIdle — but waitIdle does not reset
        // semaphore signal state, so the next iteration's acquire with the same
        // semaphore violates the spec. Guarding before acquire avoids the leak.
        if ((swapchain.extent().width == 0) || (swapchain.extent().height == 0)) {
            continue;
        }

        // Acquire next swapchain image
        uint32_t image_index{0};
        vk::Result acquire_result{vk::Result::eSuccess};
        try {
            vk::ResultValue<uint32_t> acquire{swapchain.get().acquireNextImage(std::numeric_limits<uint64_t>::max(), *image_available_semaphores[current_frame])};
            acquire_result = acquire.result;
            image_index = acquire.value;
        } catch (const vk::OutOfDateKHRError&) {
            // Swapchain recreation also rebuilds image_available_semaphores, so any
            // subtle "signalled on OUT_OF_DATE" driver behaviour can't leak to the
            // next iteration.
            device.get().waitIdle();
            if ((swapchain.extent().width > 0) && (swapchain.extent().height > 0)) {
                swapchain.recreate(swapchain.extent().width, swapchain.extent().height);
                rebuildPresentSemaphores();
                rebuildFrameBuffers();
            }
            continue;
        } catch (const vk::SurfaceLostKHRError&) {
            // Display hotplug / suspend — drain GPU work and keep trying the event loop.
            logger.logError("Surface lost during acquireNextImage — waiting for recovery.");
            device.get().waitIdle();
            continue;
        } catch (const vk::SystemError& e) {
            // Any other fatal Vulkan error (device lost, out of memory, etc.).
            logger.logFatal(std::string{"acquireNextImage failed: "} + e.what());
            std::abort();
            return;
        }

        bool swapchain_suboptimal{acquire_result == vk::Result::eSuboptimalKHR};

        // Update camera UBO and extract frustum planes. Also build the volumetric inject
        // push-constant block (needs inv_view_projection + camera_pos to reconstruct the
        // world-space position of each froxel centre).
        MathLib::Frustum frustum{};
        VolumetricInjectPushConstants inject_push{};
        inject_push.froxel_width = FROXEL_WIDTH;
        inject_push.froxel_height = FROXEL_HEIGHT;
        inject_push.froxel_depth = FROXEL_DEPTH;
        inject_push.fog_density = FOG_DENSITY;
        inject_push.fog_height_base = FOG_HEIGHT_BASE;
        inject_push.fog_height_falloff = FOG_HEIGHT_FALLOFF;
        inject_push.near_plane = FROXEL_NEAR;
        inject_push.far_plane = FROXEL_FAR;
        {
            uint32_t extent_height{swapchain.extent().height};

            // Only reset fence after we know we will submit work — resetting
            // then hitting `continue` above would leave the fence unsignalled,
            // causing waitForFences to deadlock on the next iteration.
            device.get().resetFences({*in_flight_fences[current_frame]});
            float aspect{static_cast<float>(swapchain.extent().width) / static_cast<float>(extent_height)};
            CameraUBO ubo{};
            ubo.view = camera.viewMatrix();
            ubo.projection = camera.projectionMatrix(aspect);
            MathLib::Mat4 current_vp{ubo.projection * ubo.view};
            ubo.inv_view_projection = current_vp.inversed();
            ubo.prev_view_projection = prev_view_projection;
            ubo.camera_pos = camera.position();
            ubo.frame_count = frame_counter;
            ubo.emissive_count = emissive_count;
            ubo.total_emissive_power = total_emissive_power;
            ubo.screen_width = swapchain.extent().width;
            ubo.screen_height = swapchain.extent().height;
            pipeline.updateCameraUBO(current_frame, ubo);
            prev_view_projection = current_vp;
            ++frame_counter;
            frustum = MathLib::extractFrustum(ubo.projection * ubo.view);

            inject_push.inv_view_projection = ubo.inv_view_projection;
            inject_push.camera_pos = ubo.camera_pos;
        }

        // Record command buffer — mesh shaders + compute post-process in one submission
        const vk::raii::CommandBuffer& cmd = command_buffers[current_frame];
        cmd.reset();
        updatePostProcessDescriptors(current_frame, *hdr_view, *swapchain_storage_views[image_index], *bloom_mip_views[0]);
        updateBloomDescriptors(current_frame);
        updateBloomUpsampleDescriptors(current_frame);
        updateVolumetricCompositeDescriptors(current_frame, *hdr_view);

        // Collect bloom descriptor sets for this frame (reuses pre-allocated vectors).
        bloom_sets_for_frame.clear();
        for (uint32_t m{0}; m < bloom_mip_count; ++m) {
            bloom_sets_for_frame.push_back(*bloom_descriptor_sets[current_frame * BLOOM_MAX_MIPS + m]);
        }
        bloom_upsample_sets_for_frame.clear();
        for (uint32_t m{0}; m < bloom_mip_count; ++m) {
            bloom_upsample_sets_for_frame.push_back(*bloom_upsample_descriptor_sets[current_frame * BLOOM_MAX_MIPS + m]);
        }

        recordFrame(cmd, msaa_image.image(), *msaa_view, hdr_image.image(), *hdr_view, *depth_view, depth_image.image(), swapchain.images()[image_index],
            swapchain.extent(), pipeline, pipeline.descriptorSet(current_frame), frustum, opaque_object_count, transparent_object_count, *pp_pipeline,
            *pp_pipeline_layout, *pp_descriptor_sets[current_frame], bloom_image.image(), bloom_mip_count, bloom_base_w, bloom_base_h, *bloom_extract_pipeline,
            *bloom_downsample_pipeline, *bloom_upsample_pipeline, *bloom_pipeline_layout, bloom_sets_for_frame, bloom_upsample_sets_for_frame, *skybox_pipeline,
            *skybox_pipeline_layout, *skybox_descriptor_sets[current_frame], *inject_pipeline, *inject_pipeline_layout, *inject_descriptor_sets[0], *composite_pipeline,
            *composite_pipeline_layout, *composite_descriptor_sets[current_frame], froxel_image.image(), inject_push,
            std::chrono::duration<float>(std::chrono::steady_clock::now() - start_time).count());

        // Submit
        vk::SemaphoreSubmitInfo wait_sem{};
        wait_sem.semaphore = *image_available_semaphores[current_frame];
        wait_sem.stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;

        vk::SemaphoreSubmitInfo signal_sem{};
        signal_sem.semaphore = *render_finished_semaphores[image_index];
        signal_sem.stageMask = vk::PipelineStageFlagBits2::eAllCommands;

        vk::CommandBufferSubmitInfo cmd_submit{};
        cmd_submit.commandBuffer = *cmd;

        vk::SubmitInfo2 submit_info{};
        submit_info.setWaitSemaphoreInfos(wait_sem);
        submit_info.setCommandBufferInfos(cmd_submit);
        submit_info.setSignalSemaphoreInfos(signal_sem);

        device.graphicsQueue().submit2({submit_info}, *in_flight_fences[current_frame]);

        // Present
        vk::PresentInfoKHR present_info{};
        std::array<vk::SwapchainKHR, 1> swapchains{*swapchain.get()};
        present_info.setWaitSemaphores(*render_finished_semaphores[image_index]);
        present_info.setSwapchains(swapchains);
        present_info.setPImageIndices(&image_index);

        vk::Result present_result{vk::Result::eSuccess};
        try {
            present_result = device.presentQueue().presentKHR(present_info);
        } catch (const vk::OutOfDateKHRError&) {
            // waitIdle drains in-flight GPU work before destroying swapchain resources.
            device.get().waitIdle();
            if ((swapchain.extent().width > 0) && (swapchain.extent().height > 0)) {
                swapchain.recreate(swapchain.extent().width, swapchain.extent().height);
                rebuildPresentSemaphores();
                rebuildFrameBuffers();
            }
            continue;
        }

        if ((present_result == vk::Result::eSuboptimalKHR) || swapchain_suboptimal) {
            // waitIdle drains in-flight GPU work before destroying swapchain resources.
            device.get().waitIdle();
            if ((swapchain.extent().width > 0) && (swapchain.extent().height > 0)) {
                swapchain.recreate(swapchain.extent().width, swapchain.extent().height);
                rebuildPresentSemaphores();
                rebuildFrameBuffers();
            }
        }

        current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;

        // If keys are still held, keep rendering (continuous movement)
        if (!keys_held.empty()) {
            render_cv.notify_one();
        }
    }
}

int main()
{
#if defined(_MSC_VER) && defined(_DEBUG)
    // Enable MSVC debug heap leak detection — reports C++ heap leaks on exit.
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    LoggingLib::Logger logger;

    try {
        // Create window
        WindowLib::WindowConfig config{
            .title = "TRON Grid Renderer",
            .width = 1280,
            .height = 720,
        };

        std::unique_ptr<WindowLib::Window> window{WindowLib::create(config, logger)};
        logger.logInfo("Window created: " + std::to_string(config.width) + "x" + std::to_string(config.height) + ".");

        // Vulkan initialisation
        constexpr bool ENABLE_VALIDATION =
#ifdef NDEBUG
            false;
#else
            true;
#endif

        Instance instance(ENABLE_VALIDATION, requiredSurfaceExtensions(), logger);
        vk::raii::SurfaceKHR surface{createSurface(instance.get(), *window)};
        Device device(instance, *surface, logger);

        logger.logInfo("Vulkan ready - GPU: " + device.name() + ".");

        // VMA allocator
        Allocator allocator(instance, device, logger);

        // Load shaders and create pipelines
        std::string exe_dir{executableDirectory()};
        std::vector<uint32_t> task_spirv{loadSpirv(exe_dir + "task.spv", logger)};
        std::vector<uint32_t> mesh_spirv{loadSpirv(exe_dir + "mesh.spv", logger)};

        // Create swapchain
        Swapchain swapchain(device, *surface, config.width, config.height, logger);

        // Mesh shader pipeline (task + mesh + fragment). mesh.spv is a combined Slang
        // module containing both meshMain and fragMain entry points.
        Pipeline pipeline(device, HDR_FORMAT, DEPTH_FORMAT, device.maxMsaaSamples(), task_spirv, mesh_spirv, MAX_FRAMES_IN_FLIGHT, logger);

        // Command pool + per-frame command buffers
        vk::CommandPoolCreateInfo pool_info{};
        pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        pool_info.queueFamilyIndex = device.graphicsFamilyIndex();
        vk::raii::CommandPool command_pool(device.get(), pool_info);

        vk::CommandBufferAllocateInfo alloc_info{};
        alloc_info.commandPool = *command_pool;
        alloc_info.level = vk::CommandBufferLevel::ePrimary;
        alloc_info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
        vk::raii::CommandBuffers command_buffers(device.get(), alloc_info);

        // ── Terrain generation ──

        TerrainConfig terrain_config{};
        terrain_config.grid_size = 64;
        terrain_config.tile_spacing = 1.0f;
        terrain_config.height_scale = 5.0f;
        terrain_config.noise_frequency = 0.08f;
        terrain_config.noise_octaves = 4;
        terrain_config.quantise_levels = 8;

        TerrainMesh terrain{generateTerrain(terrain_config)};
        MeshData terrain_meshlets{buildMeshlets(terrain.positions, terrain.indices)};

        uint32_t terrain_meshlet_offset{0};
        uint32_t terrain_meshlet_count{static_cast<uint32_t>(terrain_meshlets.meshlets.size())};

        logger.logInfo("Terrain: " + std::to_string(terrain.vertices.size()) + " vertices, " + std::to_string(terrain.indices.size() / 3) + " triangles, "
            + std::to_string(terrain_meshlet_count) + " meshlets.");

        // ── Neon tube geometry (thin emissive quads along grid edges) ──

        NeonTubeMesh neon_tubes{generateNeonTubes(terrain_config)};

        MeshData cyan_meshlets{buildMeshlets(neon_tubes.cyan.positions, neon_tubes.cyan.indices)};
        MeshData orange_meshlets{buildMeshlets(neon_tubes.orange.positions, neon_tubes.orange.indices)};

        logger.logInfo("Neon tubes: cyan " + std::to_string(neon_tubes.cyan.indices.size() / 3) + " tris, orange " + std::to_string(neon_tubes.orange.indices.size() / 3)
            + " tris.");

        // ── Light orb sphere ──

        constexpr float LIGHT_ORB_RADIUS{3.0f};
        MathLib::Vec3 orb_pos{0.0f, 17.0f, 0.0f};

        std::vector<MathLib::Vec3> sphere_raw_positions;
        std::vector<uint32_t> sphere_raw_indices;
        generateUVSphere(12, 24, LIGHT_ORB_RADIUS, sphere_raw_positions, sphere_raw_indices);

        // Convert indexed sphere to per-face Vertex format (smooth normals).
        std::vector<Vertex> sphere_vertices;
        std::vector<uint32_t> sphere_indices;
        std::vector<MathLib::Vec3> sphere_positions;

        uint32_t sphere_vi{0};
        for (size_t t{0}; t < sphere_raw_indices.size(); t += 3) {
            MathLib::Vec3 p0{sphere_raw_positions[sphere_raw_indices[t + 0]]};
            MathLib::Vec3 p1{sphere_raw_positions[sphere_raw_indices[t + 1]]};
            MathLib::Vec3 p2{sphere_raw_positions[sphere_raw_indices[t + 2]]};

            MathLib::Vec3 n0{p0.normalised()};
            MathLib::Vec3 n1{p1.normalised()};
            MathLib::Vec3 n2{p2.normalised()};

            sphere_positions.push_back(p0);
            sphere_positions.push_back(p1);
            sphere_positions.push_back(p2);
            sphere_vertices.push_back({{p0.x, p0.y, p0.z}, {n0.x, n0.y, n0.z}, {0.0f, 0.0f}, {n0.x, n0.y, n0.z}, 0.0f});
            sphere_vertices.push_back({{p1.x, p1.y, p1.z}, {n1.x, n1.y, n1.z}, {0.0f, 0.0f}, {n1.x, n1.y, n1.z}, 0.0f});
            sphere_vertices.push_back({{p2.x, p2.y, p2.z}, {n2.x, n2.y, n2.z}, {0.0f, 0.0f}, {n2.x, n2.y, n2.z}, 0.0f});
            sphere_indices.push_back(sphere_vi++);
            sphere_indices.push_back(sphere_vi++);
            sphere_indices.push_back(sphere_vi++);
        }

        MeshData sphere_meshlets{buildMeshlets(sphere_positions, sphere_indices)};
        uint32_t sphere_meshlet_offset{terrain_meshlet_count};
        uint32_t sphere_meshlet_count{static_cast<uint32_t>(sphere_meshlets.meshlets.size())};

        // Offset sphere meshlet vertex indices to reference the combined vertex buffer.
        uint32_t terrain_vertex_count{static_cast<uint32_t>(terrain.vertices.size())};
        for (uint32_t& idx : sphere_meshlets.vertex_indices) {
            idx += terrain_vertex_count;
        }

        // Offset sphere meshlet descriptors for combined meshlet arrays.
        uint32_t terrain_vert_idx_count{static_cast<uint32_t>(terrain_meshlets.vertex_indices.size())};
        uint32_t terrain_tri_idx_count{static_cast<uint32_t>(terrain_meshlets.triangle_indices.size())};
        for (Meshlet& m : sphere_meshlets.meshlets) {
            m.vertex_offset += terrain_vert_idx_count;
            m.triangle_offset += terrain_tri_idx_count;
        }

        // Cyan neon tube meshlet offsets — after sphere.
        uint32_t cyan_meshlet_offset{sphere_meshlet_offset + sphere_meshlet_count};
        uint32_t cyan_meshlet_count{static_cast<uint32_t>(cyan_meshlets.meshlets.size())};

        uint32_t sphere_vertex_count{static_cast<uint32_t>(sphere_vertices.size())};
        uint32_t combined_vert_count_before_cyan{terrain_vertex_count + sphere_vertex_count};
        for (uint32_t& idx : cyan_meshlets.vertex_indices) {
            idx += combined_vert_count_before_cyan;
        }

        uint32_t sphere_vert_idx_count{static_cast<uint32_t>(sphere_meshlets.vertex_indices.size())};
        uint32_t sphere_tri_idx_count{static_cast<uint32_t>(sphere_meshlets.triangle_indices.size())};
        uint32_t cum_vert_idx{terrain_vert_idx_count + sphere_vert_idx_count};
        uint32_t cum_tri_idx{terrain_tri_idx_count + sphere_tri_idx_count};
        for (Meshlet& m : cyan_meshlets.meshlets) {
            m.vertex_offset += cum_vert_idx;
            m.triangle_offset += cum_tri_idx;
        }

        // Orange neon tube meshlet offsets — after cyan.
        uint32_t orange_meshlet_offset{cyan_meshlet_offset + cyan_meshlet_count};
        uint32_t orange_meshlet_count{static_cast<uint32_t>(orange_meshlets.meshlets.size())};

        uint32_t cyan_vertex_count{static_cast<uint32_t>(neon_tubes.cyan.vertices.size())};
        uint32_t combined_vert_count_before_orange{combined_vert_count_before_cyan + cyan_vertex_count};
        for (uint32_t& idx : orange_meshlets.vertex_indices) {
            idx += combined_vert_count_before_orange;
        }

        uint32_t cyan_vert_idx_count{static_cast<uint32_t>(cyan_meshlets.vertex_indices.size())};
        uint32_t cyan_tri_idx_count{static_cast<uint32_t>(cyan_meshlets.triangle_indices.size())};
        uint32_t cum_vert_idx_2{cum_vert_idx + cyan_vert_idx_count};
        uint32_t cum_tri_idx_2{cum_tri_idx + cyan_tri_idx_count};
        for (Meshlet& m : orange_meshlets.meshlets) {
            m.vertex_offset += cum_vert_idx_2;
            m.triangle_offset += cum_tri_idx_2;
        }

        // ── Glass tower + energy-barrier pillar (Phase 8 Etape 40 transparent geometry) ──
        // The two boxes use procedural axis-aligned geometry baked at world position
        // (centre passed to generateBox). TLAS instances + scene transforms are identity
        // for both, mirroring the cyan/orange neon pattern.
        constexpr MathLib::Vec3 GLASS_TOWER_CENTRE{-15.0f, 8.0f, -15.0f};
        constexpr MathLib::Vec3 GLASS_TOWER_HALF_EXTENTS{3.0f, 8.0f, 3.0f};
        constexpr MathLib::Vec3 ENERGY_PILLAR_CENTRE{15.0f, 4.0f, 15.0f};
        constexpr MathLib::Vec3 ENERGY_PILLAR_HALF_EXTENTS{0.5f, 4.0f, 0.5f};

        Mesh glass_tower{generateBox(GLASS_TOWER_CENTRE, GLASS_TOWER_HALF_EXTENTS)};
        Mesh energy_pillar{generateBox(ENERGY_PILLAR_CENTRE, ENERGY_PILLAR_HALF_EXTENTS)};

        MeshData glass_meshlets{buildMeshlets(glass_tower.positions, glass_tower.indices)};
        MeshData pillar_meshlets{buildMeshlets(energy_pillar.positions, energy_pillar.indices)};

        // Glass tower meshlet offsets — after orange neon.
        uint32_t glass_meshlet_offset{orange_meshlet_offset + orange_meshlet_count};
        uint32_t glass_meshlet_count{static_cast<uint32_t>(glass_meshlets.meshlets.size())};

        uint32_t orange_vertex_count{static_cast<uint32_t>(neon_tubes.orange.vertices.size())};
        uint32_t combined_vert_count_before_glass{combined_vert_count_before_orange + orange_vertex_count};
        for (uint32_t& idx : glass_meshlets.vertex_indices) {
            idx += combined_vert_count_before_glass;
        }

        uint32_t orange_vert_idx_count{static_cast<uint32_t>(orange_meshlets.vertex_indices.size())};
        uint32_t orange_tri_idx_count{static_cast<uint32_t>(orange_meshlets.triangle_indices.size())};
        uint32_t cum_vert_idx_3{cum_vert_idx_2 + orange_vert_idx_count};
        uint32_t cum_tri_idx_3{cum_tri_idx_2 + orange_tri_idx_count};
        for (Meshlet& m : glass_meshlets.meshlets) {
            m.vertex_offset += cum_vert_idx_3;
            m.triangle_offset += cum_tri_idx_3;
        }

        // Energy-barrier pillar meshlet offsets — after glass tower.
        uint32_t pillar_meshlet_offset{glass_meshlet_offset + glass_meshlet_count};
        uint32_t pillar_meshlet_count{static_cast<uint32_t>(pillar_meshlets.meshlets.size())};

        uint32_t glass_vertex_count{static_cast<uint32_t>(glass_tower.vertices.size())};
        uint32_t combined_vert_count_before_pillar{combined_vert_count_before_glass + glass_vertex_count};
        for (uint32_t& idx : pillar_meshlets.vertex_indices) {
            idx += combined_vert_count_before_pillar;
        }

        uint32_t glass_vert_idx_count{static_cast<uint32_t>(glass_meshlets.vertex_indices.size())};
        uint32_t glass_tri_idx_count{static_cast<uint32_t>(glass_meshlets.triangle_indices.size())};
        uint32_t cum_vert_idx_4{cum_vert_idx_3 + glass_vert_idx_count};
        uint32_t cum_tri_idx_4{cum_tri_idx_3 + glass_tri_idx_count};
        for (Meshlet& m : pillar_meshlets.meshlets) {
            m.vertex_offset += cum_vert_idx_4;
            m.triangle_offset += cum_tri_idx_4;
        }

        // Combined vertex data — terrain + sphere + cyan neon + orange neon + glass tower + energy pillar.
        std::vector<Vertex> all_vertices;
        all_vertices.reserve(terrain.vertices.size() + sphere_vertices.size() + neon_tubes.cyan.vertices.size() + neon_tubes.orange.vertices.size()
            + glass_tower.vertices.size() + energy_pillar.vertices.size());
        all_vertices.insert(all_vertices.end(), terrain.vertices.begin(), terrain.vertices.end());
        all_vertices.insert(all_vertices.end(), sphere_vertices.begin(), sphere_vertices.end());
        all_vertices.insert(all_vertices.end(), neon_tubes.cyan.vertices.begin(), neon_tubes.cyan.vertices.end());
        all_vertices.insert(all_vertices.end(), neon_tubes.orange.vertices.begin(), neon_tubes.orange.vertices.end());
        all_vertices.insert(all_vertices.end(), glass_tower.vertices.begin(), glass_tower.vertices.end());
        all_vertices.insert(all_vertices.end(), energy_pillar.vertices.begin(), energy_pillar.vertices.end());

        logger.logInfo("Light orb: " + std::to_string(sphere_vertices.size()) + " vertices, " + std::to_string(sphere_indices.size() / 3) + " triangles, "
            + std::to_string(sphere_meshlet_count) + " meshlets.");

        // Vertex buffer — combined terrain + sphere.
        VkDeviceSize vertex_buffer_size{static_cast<VkDeviceSize>(all_vertices.size() * sizeof(Vertex))};
        AllocatedBuffer vertex_buffer{allocator.createBuffer(vertex_buffer_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst
                | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR),
            0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        {
            AllocatedBuffer vertex_staging{allocator.createBuffer(vertex_buffer_size, static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc),
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO)};
            std::memcpy(vertex_staging.allocationInfo().pMappedData, all_vertices.data(), vertex_buffer_size);

            vk::CommandBufferAllocateInfo copy_alloc{};
            copy_alloc.commandPool = *command_pool;
            copy_alloc.level = vk::CommandBufferLevel::ePrimary;
            copy_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers copy_cmds(device.get(), copy_alloc);

            const vk::raii::CommandBuffer& copy_cmd = copy_cmds[0];
            vk::CommandBufferBeginInfo copy_begin{};
            copy_begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            copy_cmd.begin(copy_begin);

            vk::BufferCopy region{};
            region.size = vertex_buffer_size;
            copy_cmd.copyBuffer(vertex_staging.buffer(), vertex_buffer.buffer(), {region});

            copy_cmd.end();

            vk::CommandBuffer raw_cmd{*copy_cmd};
            vk::SubmitInfo copy_submit{};
            copy_submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({copy_submit});
            device.graphicsQueue().waitIdle();
        }

        logger.logInfo("Vertex buffer uploaded (" + std::to_string(all_vertices.size()) + " vertices).");

        // ── Index buffer for BLAS (raw triangle indices, not meshlet indices) ──

        VkDeviceSize index_buffer_size{static_cast<VkDeviceSize>(terrain.indices.size() * sizeof(uint32_t))};
        AllocatedBuffer index_buffer{allocator.createBuffer(index_buffer_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress
                | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR),
            0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        {
            AllocatedBuffer index_staging{allocator.createBuffer(index_buffer_size, static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc),
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO)};
            std::memcpy(index_staging.allocationInfo().pMappedData, terrain.indices.data(), index_buffer_size);

            vk::CommandBufferAllocateInfo copy_alloc{};
            copy_alloc.commandPool = *command_pool;
            copy_alloc.level = vk::CommandBufferLevel::ePrimary;
            copy_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers copy_cmds(device.get(), copy_alloc);

            const vk::raii::CommandBuffer& copy_cmd = copy_cmds[0];
            vk::CommandBufferBeginInfo copy_begin{};
            copy_begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            copy_cmd.begin(copy_begin);

            vk::BufferCopy region{};
            region.size = index_buffer_size;
            copy_cmd.copyBuffer(index_staging.buffer(), index_buffer.buffer(), {region});

            copy_cmd.end();

            vk::CommandBuffer raw_cmd{*copy_cmd};
            vk::SubmitInfo copy_submit{};
            copy_submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({copy_submit});
            device.graphicsQueue().waitIdle();
        }

        logger.logInfo("Index buffer uploaded (" + std::to_string(terrain.indices.size()) + " indices).");

        // ── BLAS build for terrain ──

        vk::BufferDeviceAddressInfo vertex_addr_info{};
        vertex_addr_info.buffer = vertex_buffer.buffer();
        vk::DeviceAddress vertex_device_address{device.get().getBufferAddress(vertex_addr_info)};

        vk::BufferDeviceAddressInfo index_addr_info{};
        index_addr_info.buffer = index_buffer.buffer();
        vk::DeviceAddress index_device_address{device.get().getBufferAddress(index_addr_info)};

        vk::AccelerationStructureGeometryTrianglesDataKHR triangles_data{};
        triangles_data.vertexFormat = vk::Format::eR32G32B32Sfloat;
        triangles_data.vertexData.deviceAddress = vertex_device_address;
        triangles_data.vertexStride = sizeof(Vertex);
        triangles_data.maxVertex = static_cast<uint32_t>(terrain.vertices.size() - 1);
        triangles_data.indexType = vk::IndexType::eUint32;
        triangles_data.indexData.deviceAddress = index_device_address;

        vk::AccelerationStructureGeometryKHR geometry{};
        geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
        geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;
        geometry.geometry.triangles = triangles_data;

        vk::AccelerationStructureBuildGeometryInfoKHR build_info{};
        build_info.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
        build_info.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace | vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction;
        build_info.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        build_info.geometryCount = 1;
        build_info.setGeometries(geometry);

        uint32_t triangle_count{static_cast<uint32_t>(terrain.indices.size() / 3)};
        vk::AccelerationStructureBuildSizesInfoKHR build_sizes{
            device.get().getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, build_info, {triangle_count})};

        // Allocate AS storage buffer.
        AllocatedBuffer blas_buffer{allocator.createBuffer(build_sizes.accelerationStructureSize,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress), 0,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        vk::AccelerationStructureCreateInfoKHR as_create_info{};
        as_create_info.buffer = blas_buffer.buffer();
        as_create_info.size = build_sizes.accelerationStructureSize;
        as_create_info.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
        vk::raii::AccelerationStructureKHR blas{device.get(), as_create_info};

        build_info.dstAccelerationStructure = *blas;

        vk::AccelerationStructureBuildRangeInfoKHR range_info{};
        range_info.primitiveCount = triangle_count;

        // Build BLAS — scratch buffer scoped so it's freed after build completes.
        {
            uint32_t scratch_align{device.asScratchAlignment()};
            AllocatedBuffer scratch_buffer{allocator.createBuffer(build_sizes.buildScratchSize + scratch_align,
                static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress), 0,
                VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

            vk::BufferDeviceAddressInfo scratch_addr_info{};
            scratch_addr_info.buffer = scratch_buffer.buffer();
            vk::DeviceAddress scratch_addr{device.get().getBufferAddress(scratch_addr_info)};
            build_info.scratchData.deviceAddress = (scratch_addr + scratch_align - 1) & ~(static_cast<vk::DeviceAddress>(scratch_align) - 1);

            vk::CommandBufferAllocateInfo build_alloc{};
            build_alloc.commandPool = *command_pool;
            build_alloc.level = vk::CommandBufferLevel::ePrimary;
            build_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers build_cmds(device.get(), build_alloc);

            const vk::raii::CommandBuffer& build_cmd = build_cmds[0];
            vk::CommandBufferBeginInfo build_begin{};
            build_begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            build_cmd.begin(build_begin);

            const vk::AccelerationStructureBuildRangeInfoKHR* range_ptr{&range_info};
            build_cmd.buildAccelerationStructuresKHR({build_info}, {range_ptr});

            // Barrier: AS build write → fragment shader read.
            vk::MemoryBarrier2 as_barrier{};
            as_barrier.srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
            as_barrier.srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
            as_barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
            as_barrier.dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR;

            vk::DependencyInfo dep_info{};
            dep_info.setMemoryBarriers(as_barrier);
            build_cmd.pipelineBarrier2(dep_info);

            build_cmd.end();

            vk::CommandBuffer raw_cmd{*build_cmd};
            vk::SubmitInfo build_submit{};
            build_submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({build_submit});
            device.graphicsQueue().waitIdle();
        }

        logger.logInfo("BLAS built: " + std::to_string(triangle_count) + " triangles, " + std::to_string(build_sizes.accelerationStructureSize) + " bytes.");

        // ── Sphere index buffer + BLAS ──

        VkDeviceSize sphere_index_buffer_size{static_cast<VkDeviceSize>(sphere_indices.size() * sizeof(uint32_t))};
        AllocatedBuffer sphere_index_buffer{allocator.createBuffer(sphere_index_buffer_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress
                | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR),
            0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        {
            AllocatedBuffer sphere_idx_staging{allocator.createBuffer(sphere_index_buffer_size, static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc),
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO)};
            std::memcpy(sphere_idx_staging.allocationInfo().pMappedData, sphere_indices.data(), sphere_index_buffer_size);

            vk::CommandBufferAllocateInfo copy_alloc{};
            copy_alloc.commandPool = *command_pool;
            copy_alloc.level = vk::CommandBufferLevel::ePrimary;
            copy_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers copy_cmds(device.get(), copy_alloc);

            const vk::raii::CommandBuffer& copy_cmd = copy_cmds[0];
            vk::CommandBufferBeginInfo copy_begin{};
            copy_begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            copy_cmd.begin(copy_begin);

            vk::BufferCopy region{};
            region.size = sphere_index_buffer_size;
            copy_cmd.copyBuffer(sphere_idx_staging.buffer(), sphere_index_buffer.buffer(), {region});

            copy_cmd.end();

            vk::CommandBuffer raw_cmd{*copy_cmd};
            vk::SubmitInfo copy_submit{};
            copy_submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({copy_submit});
            device.graphicsQueue().waitIdle();
        }

        // Sphere BLAS — references the sphere portion of the combined vertex buffer.
        vk::DeviceAddress sphere_vertex_address{vertex_device_address + static_cast<vk::DeviceSize>(terrain_vertex_count * sizeof(Vertex))};

        vk::BufferDeviceAddressInfo sphere_index_addr_info{};
        sphere_index_addr_info.buffer = sphere_index_buffer.buffer();
        vk::DeviceAddress sphere_index_device_address{device.get().getBufferAddress(sphere_index_addr_info)};

        vk::AccelerationStructureGeometryTrianglesDataKHR sphere_tri_data{};
        sphere_tri_data.vertexFormat = vk::Format::eR32G32B32Sfloat;
        sphere_tri_data.vertexData.deviceAddress = sphere_vertex_address;
        sphere_tri_data.vertexStride = sizeof(Vertex);
        sphere_tri_data.maxVertex = static_cast<uint32_t>(sphere_vertices.size() - 1);
        sphere_tri_data.indexType = vk::IndexType::eUint32;
        sphere_tri_data.indexData.deviceAddress = sphere_index_device_address;

        vk::AccelerationStructureGeometryKHR sphere_geometry{};
        sphere_geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
        sphere_geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;
        sphere_geometry.geometry.triangles = sphere_tri_data;

        vk::AccelerationStructureBuildGeometryInfoKHR sphere_build_info{};
        sphere_build_info.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
        sphere_build_info.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
        sphere_build_info.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        sphere_build_info.geometryCount = 1;
        sphere_build_info.setGeometries(sphere_geometry);

        uint32_t sphere_triangle_count{static_cast<uint32_t>(sphere_indices.size() / 3)};
        vk::AccelerationStructureBuildSizesInfoKHR sphere_build_sizes{
            device.get().getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, sphere_build_info, {sphere_triangle_count})};

        AllocatedBuffer sphere_blas_buffer{allocator.createBuffer(sphere_build_sizes.accelerationStructureSize,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress), 0,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        vk::AccelerationStructureCreateInfoKHR sphere_as_create_info{};
        sphere_as_create_info.buffer = sphere_blas_buffer.buffer();
        sphere_as_create_info.size = sphere_build_sizes.accelerationStructureSize;
        sphere_as_create_info.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
        vk::raii::AccelerationStructureKHR sphere_blas{device.get(), sphere_as_create_info};

        sphere_build_info.dstAccelerationStructure = *sphere_blas;

        AllocatedBuffer sphere_scratch{allocator.createBuffer(sphere_build_sizes.buildScratchSize + device.asScratchAlignment(),
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress), 0,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        vk::BufferDeviceAddressInfo sphere_scratch_addr_info{};
        sphere_scratch_addr_info.buffer = sphere_scratch.buffer();
        vk::DeviceAddress sphere_scratch_addr{device.get().getBufferAddress(sphere_scratch_addr_info)};
        vk::DeviceAddress align_mask{static_cast<vk::DeviceAddress>(device.asScratchAlignment()) - 1};
        sphere_build_info.scratchData.deviceAddress = (sphere_scratch_addr + align_mask) & ~align_mask;

        {
            vk::CommandBufferAllocateInfo cmd_alloc{};
            cmd_alloc.commandPool = *command_pool;
            cmd_alloc.level = vk::CommandBufferLevel::ePrimary;
            cmd_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers cmds(device.get(), cmd_alloc);

            const vk::raii::CommandBuffer& build_cmd = cmds[0];
            vk::CommandBufferBeginInfo begin_info{};
            begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            build_cmd.begin(begin_info);

            vk::AccelerationStructureBuildRangeInfoKHR sphere_range{};
            sphere_range.primitiveCount = sphere_triangle_count;
            const vk::AccelerationStructureBuildRangeInfoKHR* sphere_range_ptr{&sphere_range};
            build_cmd.buildAccelerationStructuresKHR({sphere_build_info}, {sphere_range_ptr});

            vk::MemoryBarrier2 as_barrier{};
            as_barrier.srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
            as_barrier.srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
            as_barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
            as_barrier.dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR;

            vk::DependencyInfo dep_info{};
            dep_info.setMemoryBarriers(as_barrier);
            build_cmd.pipelineBarrier2(dep_info);

            build_cmd.end();

            vk::CommandBuffer raw_cmd{*build_cmd};
            vk::SubmitInfo build_submit{};
            build_submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({build_submit});
            device.graphicsQueue().waitIdle();
        }

        logger.logInfo("Sphere BLAS built: " + std::to_string(sphere_triangle_count) + " triangles.");

        // ── Cyan neon tube index buffer + BLAS ──

        VkDeviceSize cyan_index_buffer_size{static_cast<VkDeviceSize>(neon_tubes.cyan.indices.size() * sizeof(uint32_t))};
        AllocatedBuffer cyan_index_buffer{allocator.createBuffer(cyan_index_buffer_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress
                | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR),
            0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        {
            AllocatedBuffer cyan_idx_staging{allocator.createBuffer(cyan_index_buffer_size, static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc),
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO)};
            std::memcpy(cyan_idx_staging.allocationInfo().pMappedData, neon_tubes.cyan.indices.data(), cyan_index_buffer_size);

            vk::CommandBufferAllocateInfo copy_alloc{};
            copy_alloc.commandPool = *command_pool;
            copy_alloc.level = vk::CommandBufferLevel::ePrimary;
            copy_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers copy_cmds(device.get(), copy_alloc);

            const vk::raii::CommandBuffer& copy_cmd = copy_cmds[0];
            vk::CommandBufferBeginInfo copy_begin{};
            copy_begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            copy_cmd.begin(copy_begin);

            vk::BufferCopy region{};
            region.size = cyan_index_buffer_size;
            copy_cmd.copyBuffer(cyan_idx_staging.buffer(), cyan_index_buffer.buffer(), {region});

            copy_cmd.end();

            vk::CommandBuffer raw_cmd{*copy_cmd};
            vk::SubmitInfo copy_submit{};
            copy_submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({copy_submit});
            device.graphicsQueue().waitIdle();
        }

        // Cyan BLAS — references cyan neon portion of combined vertex buffer.
        vk::DeviceAddress cyan_vertex_address{vertex_device_address + static_cast<vk::DeviceSize>(combined_vert_count_before_cyan * sizeof(Vertex))};

        vk::BufferDeviceAddressInfo cyan_index_addr_info{};
        cyan_index_addr_info.buffer = cyan_index_buffer.buffer();
        vk::DeviceAddress cyan_index_device_address{device.get().getBufferAddress(cyan_index_addr_info)};

        vk::AccelerationStructureGeometryTrianglesDataKHR cyan_tri_data{};
        cyan_tri_data.vertexFormat = vk::Format::eR32G32B32Sfloat;
        cyan_tri_data.vertexData.deviceAddress = cyan_vertex_address;
        cyan_tri_data.vertexStride = sizeof(Vertex);
        cyan_tri_data.maxVertex = static_cast<uint32_t>(neon_tubes.cyan.vertices.size() - 1);
        cyan_tri_data.indexType = vk::IndexType::eUint32;
        cyan_tri_data.indexData.deviceAddress = cyan_index_device_address;

        vk::AccelerationStructureGeometryKHR cyan_geometry{};
        cyan_geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
        cyan_geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;
        cyan_geometry.geometry.triangles = cyan_tri_data;

        vk::AccelerationStructureBuildGeometryInfoKHR cyan_build_info{};
        cyan_build_info.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
        cyan_build_info.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
        cyan_build_info.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        cyan_build_info.geometryCount = 1;
        cyan_build_info.setGeometries(cyan_geometry);

        uint32_t cyan_triangle_count{static_cast<uint32_t>(neon_tubes.cyan.indices.size() / 3)};
        vk::AccelerationStructureBuildSizesInfoKHR cyan_build_sizes{
            device.get().getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, cyan_build_info, {cyan_triangle_count})};

        AllocatedBuffer cyan_blas_buffer{allocator.createBuffer(cyan_build_sizes.accelerationStructureSize,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress), 0,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        vk::AccelerationStructureCreateInfoKHR cyan_as_create{};
        cyan_as_create.buffer = cyan_blas_buffer.buffer();
        cyan_as_create.size = cyan_build_sizes.accelerationStructureSize;
        cyan_as_create.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
        vk::raii::AccelerationStructureKHR cyan_blas{device.get(), cyan_as_create};

        cyan_build_info.dstAccelerationStructure = *cyan_blas;

        {
            uint32_t scratch_align{device.asScratchAlignment()};
            AllocatedBuffer scratch{allocator.createBuffer(cyan_build_sizes.buildScratchSize + scratch_align,
                static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress), 0,
                VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

            vk::BufferDeviceAddressInfo scratch_addr_info{};
            scratch_addr_info.buffer = scratch.buffer();
            vk::DeviceAddress scratch_addr{device.get().getBufferAddress(scratch_addr_info)};
            vk::DeviceAddress as_align_mask{static_cast<vk::DeviceAddress>(scratch_align) - 1};
            cyan_build_info.scratchData.deviceAddress = (scratch_addr + as_align_mask) & ~as_align_mask;

            vk::CommandBufferAllocateInfo cmd_alloc{};
            cmd_alloc.commandPool = *command_pool;
            cmd_alloc.level = vk::CommandBufferLevel::ePrimary;
            cmd_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers cmds(device.get(), cmd_alloc);

            const vk::raii::CommandBuffer& build_cmd = cmds[0];
            vk::CommandBufferBeginInfo begin_info{};
            begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            build_cmd.begin(begin_info);

            vk::AccelerationStructureBuildRangeInfoKHR cyan_range{};
            cyan_range.primitiveCount = cyan_triangle_count;
            const vk::AccelerationStructureBuildRangeInfoKHR* cyan_range_ptr{&cyan_range};
            build_cmd.buildAccelerationStructuresKHR({cyan_build_info}, {cyan_range_ptr});

            vk::MemoryBarrier2 as_barrier{};
            as_barrier.srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
            as_barrier.srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
            as_barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
            as_barrier.dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR;

            vk::DependencyInfo dep_info{};
            dep_info.setMemoryBarriers(as_barrier);
            build_cmd.pipelineBarrier2(dep_info);

            build_cmd.end();

            vk::CommandBuffer raw_cmd{*build_cmd};
            vk::SubmitInfo build_submit{};
            build_submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({build_submit});
            device.graphicsQueue().waitIdle();
        }

        logger.logInfo("Cyan neon BLAS built: " + std::to_string(cyan_triangle_count) + " triangles.");

        // ── Orange neon tube index buffer + BLAS ──

        VkDeviceSize orange_index_buffer_size{static_cast<VkDeviceSize>(neon_tubes.orange.indices.size() * sizeof(uint32_t))};
        AllocatedBuffer orange_index_buffer{allocator.createBuffer(orange_index_buffer_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress
                | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR),
            0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        {
            AllocatedBuffer orange_idx_staging{allocator.createBuffer(orange_index_buffer_size, static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc),
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO)};
            std::memcpy(orange_idx_staging.allocationInfo().pMappedData, neon_tubes.orange.indices.data(), orange_index_buffer_size);

            vk::CommandBufferAllocateInfo copy_alloc{};
            copy_alloc.commandPool = *command_pool;
            copy_alloc.level = vk::CommandBufferLevel::ePrimary;
            copy_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers copy_cmds(device.get(), copy_alloc);

            const vk::raii::CommandBuffer& copy_cmd = copy_cmds[0];
            vk::CommandBufferBeginInfo copy_begin{};
            copy_begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            copy_cmd.begin(copy_begin);

            vk::BufferCopy region{};
            region.size = orange_index_buffer_size;
            copy_cmd.copyBuffer(orange_idx_staging.buffer(), orange_index_buffer.buffer(), {region});

            copy_cmd.end();

            vk::CommandBuffer raw_cmd{*copy_cmd};
            vk::SubmitInfo copy_submit{};
            copy_submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({copy_submit});
            device.graphicsQueue().waitIdle();
        }

        // Orange BLAS — references orange neon portion of combined vertex buffer.
        vk::DeviceAddress orange_vertex_address{vertex_device_address + static_cast<vk::DeviceSize>(combined_vert_count_before_orange * sizeof(Vertex))};

        vk::BufferDeviceAddressInfo orange_index_addr_info{};
        orange_index_addr_info.buffer = orange_index_buffer.buffer();
        vk::DeviceAddress orange_index_device_address{device.get().getBufferAddress(orange_index_addr_info)};

        vk::AccelerationStructureGeometryTrianglesDataKHR orange_tri_data{};
        orange_tri_data.vertexFormat = vk::Format::eR32G32B32Sfloat;
        orange_tri_data.vertexData.deviceAddress = orange_vertex_address;
        orange_tri_data.vertexStride = sizeof(Vertex);
        orange_tri_data.maxVertex = static_cast<uint32_t>(neon_tubes.orange.vertices.size() - 1);
        orange_tri_data.indexType = vk::IndexType::eUint32;
        orange_tri_data.indexData.deviceAddress = orange_index_device_address;

        vk::AccelerationStructureGeometryKHR orange_geometry{};
        orange_geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
        orange_geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;
        orange_geometry.geometry.triangles = orange_tri_data;

        vk::AccelerationStructureBuildGeometryInfoKHR orange_build_info{};
        orange_build_info.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
        orange_build_info.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
        orange_build_info.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        orange_build_info.geometryCount = 1;
        orange_build_info.setGeometries(orange_geometry);

        uint32_t orange_triangle_count{static_cast<uint32_t>(neon_tubes.orange.indices.size() / 3)};
        vk::AccelerationStructureBuildSizesInfoKHR orange_build_sizes{
            device.get().getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, orange_build_info, {orange_triangle_count})};

        AllocatedBuffer orange_blas_buffer{allocator.createBuffer(orange_build_sizes.accelerationStructureSize,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress), 0,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        vk::AccelerationStructureCreateInfoKHR orange_as_create{};
        orange_as_create.buffer = orange_blas_buffer.buffer();
        orange_as_create.size = orange_build_sizes.accelerationStructureSize;
        orange_as_create.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
        vk::raii::AccelerationStructureKHR orange_blas{device.get(), orange_as_create};

        orange_build_info.dstAccelerationStructure = *orange_blas;

        {
            uint32_t scratch_align{device.asScratchAlignment()};
            AllocatedBuffer scratch{allocator.createBuffer(orange_build_sizes.buildScratchSize + scratch_align,
                static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress), 0,
                VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

            vk::BufferDeviceAddressInfo scratch_addr_info{};
            scratch_addr_info.buffer = scratch.buffer();
            vk::DeviceAddress scratch_addr{device.get().getBufferAddress(scratch_addr_info)};
            vk::DeviceAddress as_align_mask{static_cast<vk::DeviceAddress>(scratch_align) - 1};
            orange_build_info.scratchData.deviceAddress = (scratch_addr + as_align_mask) & ~as_align_mask;

            vk::CommandBufferAllocateInfo cmd_alloc{};
            cmd_alloc.commandPool = *command_pool;
            cmd_alloc.level = vk::CommandBufferLevel::ePrimary;
            cmd_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers cmds(device.get(), cmd_alloc);

            const vk::raii::CommandBuffer& build_cmd = cmds[0];
            vk::CommandBufferBeginInfo begin_info{};
            begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            build_cmd.begin(begin_info);

            vk::AccelerationStructureBuildRangeInfoKHR orange_range{};
            orange_range.primitiveCount = orange_triangle_count;
            const vk::AccelerationStructureBuildRangeInfoKHR* orange_range_ptr{&orange_range};
            build_cmd.buildAccelerationStructuresKHR({orange_build_info}, {orange_range_ptr});

            vk::MemoryBarrier2 as_barrier{};
            as_barrier.srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
            as_barrier.srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
            as_barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
            as_barrier.dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR;

            vk::DependencyInfo dep_info{};
            dep_info.setMemoryBarriers(as_barrier);
            build_cmd.pipelineBarrier2(dep_info);

            build_cmd.end();

            vk::CommandBuffer raw_cmd{*build_cmd};
            vk::SubmitInfo build_submit{};
            build_submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({build_submit});
            device.graphicsQueue().waitIdle();
        }

        logger.logInfo("Orange neon BLAS built: " + std::to_string(orange_triangle_count) + " triangles.");

        // ── Glass tower index buffer + BLAS ──

        VkDeviceSize glass_index_buffer_size{static_cast<VkDeviceSize>(glass_tower.indices.size() * sizeof(uint32_t))};
        AllocatedBuffer glass_index_buffer{allocator.createBuffer(glass_index_buffer_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress
                | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR),
            0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        {
            AllocatedBuffer glass_idx_staging{allocator.createBuffer(glass_index_buffer_size, static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc),
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO)};
            std::memcpy(glass_idx_staging.allocationInfo().pMappedData, glass_tower.indices.data(), glass_index_buffer_size);

            vk::CommandBufferAllocateInfo copy_alloc{};
            copy_alloc.commandPool = *command_pool;
            copy_alloc.level = vk::CommandBufferLevel::ePrimary;
            copy_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers copy_cmds(device.get(), copy_alloc);

            const vk::raii::CommandBuffer& copy_cmd = copy_cmds[0];
            vk::CommandBufferBeginInfo copy_begin{};
            copy_begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            copy_cmd.begin(copy_begin);

            vk::BufferCopy region{};
            region.size = glass_index_buffer_size;
            copy_cmd.copyBuffer(glass_idx_staging.buffer(), glass_index_buffer.buffer(), {region});

            copy_cmd.end();

            vk::CommandBuffer raw_cmd{*copy_cmd};
            vk::SubmitInfo copy_submit{};
            copy_submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({copy_submit});
            device.graphicsQueue().waitIdle();
        }

        vk::DeviceAddress glass_vertex_address{vertex_device_address + static_cast<vk::DeviceSize>(combined_vert_count_before_glass * sizeof(Vertex))};

        vk::BufferDeviceAddressInfo glass_index_addr_info{};
        glass_index_addr_info.buffer = glass_index_buffer.buffer();
        vk::DeviceAddress glass_index_device_address{device.get().getBufferAddress(glass_index_addr_info)};

        vk::AccelerationStructureGeometryTrianglesDataKHR glass_tri_data{};
        glass_tri_data.vertexFormat = vk::Format::eR32G32B32Sfloat;
        glass_tri_data.vertexData.deviceAddress = glass_vertex_address;
        glass_tri_data.vertexStride = sizeof(Vertex);
        glass_tri_data.maxVertex = static_cast<uint32_t>(glass_tower.vertices.size() - 1);
        glass_tri_data.indexType = vk::IndexType::eUint32;
        glass_tri_data.indexData.deviceAddress = glass_index_device_address;

        vk::AccelerationStructureGeometryKHR glass_geometry{};
        glass_geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
        // Phase 8 Etape 40c: glass + pillar BLAS geometries intentionally OMIT the eOpaque
        // flag so that inline ray queries fired from fragTransparent receive each candidate
        // hit through Proceed() rather than auto-committing. This lets the refraction ray
        // skip its own originating instance — a refraction ray fired from the glass tower's
        // front face would otherwise immediately re-hit the tower's back face and sample
        // the glass material instead of the terrain behind. All other BLASes (terrain, orb,
        // cyan / orange neon) keep eOpaque for traversal performance — transparents are the
        // only geometries that need the candidate-filter path.
        glass_geometry.geometry.triangles = glass_tri_data;

        vk::AccelerationStructureBuildGeometryInfoKHR glass_build_info{};
        glass_build_info.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
        glass_build_info.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
        glass_build_info.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        glass_build_info.geometryCount = 1;
        glass_build_info.setGeometries(glass_geometry);

        uint32_t glass_triangle_count{static_cast<uint32_t>(glass_tower.indices.size() / 3)};
        vk::AccelerationStructureBuildSizesInfoKHR glass_build_sizes{
            device.get().getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, glass_build_info, {glass_triangle_count})};

        AllocatedBuffer glass_blas_buffer{allocator.createBuffer(glass_build_sizes.accelerationStructureSize,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress), 0,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        vk::AccelerationStructureCreateInfoKHR glass_as_create{};
        glass_as_create.buffer = glass_blas_buffer.buffer();
        glass_as_create.size = glass_build_sizes.accelerationStructureSize;
        glass_as_create.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
        vk::raii::AccelerationStructureKHR glass_blas{device.get(), glass_as_create};

        glass_build_info.dstAccelerationStructure = *glass_blas;

        {
            uint32_t scratch_align{device.asScratchAlignment()};
            AllocatedBuffer scratch{allocator.createBuffer(glass_build_sizes.buildScratchSize + scratch_align,
                static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress), 0,
                VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

            vk::BufferDeviceAddressInfo scratch_addr_info{};
            scratch_addr_info.buffer = scratch.buffer();
            vk::DeviceAddress scratch_addr{device.get().getBufferAddress(scratch_addr_info)};
            vk::DeviceAddress as_align_mask{static_cast<vk::DeviceAddress>(scratch_align) - 1};
            glass_build_info.scratchData.deviceAddress = (scratch_addr + as_align_mask) & ~as_align_mask;

            vk::CommandBufferAllocateInfo cmd_alloc{};
            cmd_alloc.commandPool = *command_pool;
            cmd_alloc.level = vk::CommandBufferLevel::ePrimary;
            cmd_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers cmds(device.get(), cmd_alloc);

            const vk::raii::CommandBuffer& build_cmd = cmds[0];
            vk::CommandBufferBeginInfo begin_info{};
            begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            build_cmd.begin(begin_info);

            vk::AccelerationStructureBuildRangeInfoKHR glass_range{};
            glass_range.primitiveCount = glass_triangle_count;
            const vk::AccelerationStructureBuildRangeInfoKHR* glass_range_ptr{&glass_range};
            build_cmd.buildAccelerationStructuresKHR({glass_build_info}, {glass_range_ptr});

            vk::MemoryBarrier2 as_barrier{};
            as_barrier.srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
            as_barrier.srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
            as_barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
            as_barrier.dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR;

            vk::DependencyInfo dep_info{};
            dep_info.setMemoryBarriers(as_barrier);
            build_cmd.pipelineBarrier2(dep_info);

            build_cmd.end();

            vk::CommandBuffer raw_cmd{*build_cmd};
            vk::SubmitInfo build_submit{};
            build_submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({build_submit});
            device.graphicsQueue().waitIdle();
        }

        logger.logInfo("Glass tower BLAS built: " + std::to_string(glass_triangle_count) + " triangles.");

        // ── Energy-barrier pillar index buffer + BLAS ──

        VkDeviceSize pillar_index_buffer_size{static_cast<VkDeviceSize>(energy_pillar.indices.size() * sizeof(uint32_t))};
        AllocatedBuffer pillar_index_buffer{allocator.createBuffer(pillar_index_buffer_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress
                | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR),
            0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        {
            AllocatedBuffer pillar_idx_staging{allocator.createBuffer(pillar_index_buffer_size, static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc),
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO)};
            std::memcpy(pillar_idx_staging.allocationInfo().pMappedData, energy_pillar.indices.data(), pillar_index_buffer_size);

            vk::CommandBufferAllocateInfo copy_alloc{};
            copy_alloc.commandPool = *command_pool;
            copy_alloc.level = vk::CommandBufferLevel::ePrimary;
            copy_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers copy_cmds(device.get(), copy_alloc);

            const vk::raii::CommandBuffer& copy_cmd = copy_cmds[0];
            vk::CommandBufferBeginInfo copy_begin{};
            copy_begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            copy_cmd.begin(copy_begin);

            vk::BufferCopy region{};
            region.size = pillar_index_buffer_size;
            copy_cmd.copyBuffer(pillar_idx_staging.buffer(), pillar_index_buffer.buffer(), {region});

            copy_cmd.end();

            vk::CommandBuffer raw_cmd{*copy_cmd};
            vk::SubmitInfo copy_submit{};
            copy_submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({copy_submit});
            device.graphicsQueue().waitIdle();
        }

        vk::DeviceAddress pillar_vertex_address{vertex_device_address + static_cast<vk::DeviceSize>(combined_vert_count_before_pillar * sizeof(Vertex))};

        vk::BufferDeviceAddressInfo pillar_index_addr_info{};
        pillar_index_addr_info.buffer = pillar_index_buffer.buffer();
        vk::DeviceAddress pillar_index_device_address{device.get().getBufferAddress(pillar_index_addr_info)};

        vk::AccelerationStructureGeometryTrianglesDataKHR pillar_tri_data{};
        pillar_tri_data.vertexFormat = vk::Format::eR32G32B32Sfloat;
        pillar_tri_data.vertexData.deviceAddress = pillar_vertex_address;
        pillar_tri_data.vertexStride = sizeof(Vertex);
        pillar_tri_data.maxVertex = static_cast<uint32_t>(energy_pillar.vertices.size() - 1);
        pillar_tri_data.indexType = vk::IndexType::eUint32;
        pillar_tri_data.indexData.deviceAddress = pillar_index_device_address;

        vk::AccelerationStructureGeometryKHR pillar_geometry{};
        pillar_geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
        // Non-opaque for the same Phase 8 Etape 40c reason as the glass tower BLAS — see
        // glass_geometry.flags comment above.
        pillar_geometry.geometry.triangles = pillar_tri_data;

        vk::AccelerationStructureBuildGeometryInfoKHR pillar_build_info{};
        pillar_build_info.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
        pillar_build_info.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
        pillar_build_info.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        pillar_build_info.geometryCount = 1;
        pillar_build_info.setGeometries(pillar_geometry);

        uint32_t pillar_triangle_count{static_cast<uint32_t>(energy_pillar.indices.size() / 3)};
        vk::AccelerationStructureBuildSizesInfoKHR pillar_build_sizes{
            device.get().getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, pillar_build_info, {pillar_triangle_count})};

        AllocatedBuffer pillar_blas_buffer{allocator.createBuffer(pillar_build_sizes.accelerationStructureSize,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress), 0,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        vk::AccelerationStructureCreateInfoKHR pillar_as_create{};
        pillar_as_create.buffer = pillar_blas_buffer.buffer();
        pillar_as_create.size = pillar_build_sizes.accelerationStructureSize;
        pillar_as_create.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
        vk::raii::AccelerationStructureKHR pillar_blas{device.get(), pillar_as_create};

        pillar_build_info.dstAccelerationStructure = *pillar_blas;

        {
            uint32_t scratch_align{device.asScratchAlignment()};
            AllocatedBuffer scratch{allocator.createBuffer(pillar_build_sizes.buildScratchSize + scratch_align,
                static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress), 0,
                VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

            vk::BufferDeviceAddressInfo scratch_addr_info{};
            scratch_addr_info.buffer = scratch.buffer();
            vk::DeviceAddress scratch_addr{device.get().getBufferAddress(scratch_addr_info)};
            vk::DeviceAddress as_align_mask{static_cast<vk::DeviceAddress>(scratch_align) - 1};
            pillar_build_info.scratchData.deviceAddress = (scratch_addr + as_align_mask) & ~as_align_mask;

            vk::CommandBufferAllocateInfo cmd_alloc{};
            cmd_alloc.commandPool = *command_pool;
            cmd_alloc.level = vk::CommandBufferLevel::ePrimary;
            cmd_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers cmds(device.get(), cmd_alloc);

            const vk::raii::CommandBuffer& build_cmd = cmds[0];
            vk::CommandBufferBeginInfo begin_info{};
            begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            build_cmd.begin(begin_info);

            vk::AccelerationStructureBuildRangeInfoKHR pillar_range{};
            pillar_range.primitiveCount = pillar_triangle_count;
            const vk::AccelerationStructureBuildRangeInfoKHR* pillar_range_ptr{&pillar_range};
            build_cmd.buildAccelerationStructuresKHR({pillar_build_info}, {pillar_range_ptr});

            vk::MemoryBarrier2 as_barrier{};
            as_barrier.srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
            as_barrier.srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
            as_barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
            as_barrier.dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR;

            vk::DependencyInfo dep_info{};
            dep_info.setMemoryBarriers(as_barrier);
            build_cmd.pipelineBarrier2(dep_info);

            build_cmd.end();

            vk::CommandBuffer raw_cmd{*build_cmd};
            vk::SubmitInfo build_submit{};
            build_submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({build_submit});
            device.graphicsQueue().waitIdle();
        }

        logger.logInfo("Energy-barrier pillar BLAS built: " + std::to_string(pillar_triangle_count) + " triangles.");

        // Scene — terrain at origin, orb above, neon tubes at origin, glass tower
        // and energy-barrier pillar baked at their world positions (identity transforms).
        // Bounding radii for the boxes are conservative bounding spheres around their
        // half-extents, computed from world centre.
        constexpr float GLASS_TOWER_BOUNDING_RADIUS{9.0554f}; //!< sqrt(3^2 + 8^2 + 3^2) ≈ 9.0554, matches GLASS_TOWER_HALF_EXTENTS.
        constexpr float ENERGY_PILLAR_BOUNDING_RADIUS{4.0623f}; //!< sqrt(0.5^2 + 4^2 + 0.5^2) ≈ 4.0623, matches ENERGY_PILLAR_HALF_EXTENTS.

        Scene scene;
        Transform terrain_transform{};
        (void)scene.addEntity(terrain_transform, {0}, {{0.0f, 0.0f, 0.0f}, terrain.bounding_radius});

        Transform orb_transform{};
        orb_transform.position = orb_pos;
        (void)scene.addEntity(orb_transform, {1}, {orb_pos, LIGHT_ORB_RADIUS});

        Transform cyan_neon_transform{};
        (void)scene.addEntity(cyan_neon_transform, {2}, {{0.0f, 0.0f, 0.0f}, neon_tubes.bounding_radius});

        Transform orange_neon_transform{};
        (void)scene.addEntity(orange_neon_transform, {3}, {{0.0f, 0.0f, 0.0f}, neon_tubes.bounding_radius});

        Transform glass_tower_transform{};
        (void)scene.addEntity(glass_tower_transform, {4}, {GLASS_TOWER_CENTRE, GLASS_TOWER_BOUNDING_RADIUS});

        Transform energy_pillar_transform{};
        (void)scene.addEntity(energy_pillar_transform, {5}, {ENERGY_PILLAR_CENTRE, ENERGY_PILLAR_BOUNDING_RADIUS});

        uint32_t total_objects{scene.entityCount()};

        // ── TLAS build ──

        // Get BLAS device addresses.
        vk::AccelerationStructureDeviceAddressInfoKHR terrain_blas_addr_info{};
        terrain_blas_addr_info.accelerationStructure = *blas;
        vk::DeviceAddress terrain_blas_address{device.get().getAccelerationStructureAddressKHR(terrain_blas_addr_info)};

        vk::AccelerationStructureDeviceAddressInfoKHR sphere_blas_addr_info{};
        sphere_blas_addr_info.accelerationStructure = *sphere_blas;
        vk::DeviceAddress sphere_blas_address{device.get().getAccelerationStructureAddressKHR(sphere_blas_addr_info)};

        vk::AccelerationStructureDeviceAddressInfoKHR cyan_blas_addr_info{};
        cyan_blas_addr_info.accelerationStructure = *cyan_blas;
        vk::DeviceAddress cyan_blas_address{device.get().getAccelerationStructureAddressKHR(cyan_blas_addr_info)};

        vk::AccelerationStructureDeviceAddressInfoKHR orange_blas_addr_info{};
        orange_blas_addr_info.accelerationStructure = *orange_blas;
        vk::DeviceAddress orange_blas_address{device.get().getAccelerationStructureAddressKHR(orange_blas_addr_info)};

        vk::AccelerationStructureDeviceAddressInfoKHR glass_blas_addr_info{};
        glass_blas_addr_info.accelerationStructure = *glass_blas;
        vk::DeviceAddress glass_blas_address{device.get().getAccelerationStructureAddressKHR(glass_blas_addr_info)};

        vk::AccelerationStructureDeviceAddressInfoKHR pillar_blas_addr_info{};
        pillar_blas_addr_info.accelerationStructure = *pillar_blas;
        vk::DeviceAddress pillar_blas_address{device.get().getAccelerationStructureAddressKHR(pillar_blas_addr_info)};

        // BLAS reference per mesh ID — 0=terrain, 1=sphere, 2=cyan neon, 3=orange neon,
        // 4=glass tower, 5=energy-barrier pillar.
        std::vector<vk::DeviceAddress> blas_per_mesh{
            terrain_blas_address, sphere_blas_address, cyan_blas_address, orange_blas_address, glass_blas_address, pillar_blas_address};

        // Build instance data — one per scene entity.
        std::vector<VkAccelerationStructureInstanceKHR> as_instances;
        as_instances.reserve(total_objects);
        for (uint32_t i{0}; i < total_objects; ++i) {
            MathLib::Mat4 model{scene.transforms()[i].modelMatrix()};

            // VkTransformMatrixKHR is 3×4 row-major (transposed from our column-major Mat4).
            VkTransformMatrixKHR transform{};
            for (uint32_t row{0}; row < 3; ++row) {
                for (uint32_t col{0}; col < 4; ++col) {
                    transform.matrix[row][col] = model(col, row);
                }
            }

            uint32_t mesh_id{scene.meshIDs()[i].id};

            VkAccelerationStructureInstanceKHR as_inst{};
            as_inst.transform = transform;
            as_inst.instanceCustomIndex = i;
            as_inst.mask = 0xFF;
            as_inst.instanceShaderBindingTableRecordOffset = 0;
            as_inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            as_inst.accelerationStructureReference = blas_per_mesh[mesh_id];
            as_instances.push_back(as_inst);
        }

        // Upload instance data to GPU.
        VkDeviceSize instance_buffer_size{static_cast<VkDeviceSize>(as_instances.size() * sizeof(VkAccelerationStructureInstanceKHR))};
        AllocatedBuffer instance_buffer{allocator.createBuffer(instance_buffer_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR
                | vk::BufferUsageFlagBits::eTransferDst),
            0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        {
            AllocatedBuffer instance_staging{allocator.createBuffer(instance_buffer_size, static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc),
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO)};
            std::memcpy(instance_staging.allocationInfo().pMappedData, as_instances.data(), instance_buffer_size);

            vk::CommandBufferAllocateInfo copy_alloc{};
            copy_alloc.commandPool = *command_pool;
            copy_alloc.level = vk::CommandBufferLevel::ePrimary;
            copy_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers copy_cmds(device.get(), copy_alloc);

            const vk::raii::CommandBuffer& copy_cmd = copy_cmds[0];
            vk::CommandBufferBeginInfo copy_begin{};
            copy_begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            copy_cmd.begin(copy_begin);

            vk::BufferCopy region{};
            region.size = instance_buffer_size;
            copy_cmd.copyBuffer(instance_staging.buffer(), instance_buffer.buffer(), {region});

            copy_cmd.end();

            vk::CommandBuffer raw_cmd{*copy_cmd};
            vk::SubmitInfo copy_submit{};
            copy_submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({copy_submit});
            device.graphicsQueue().waitIdle();
        }

        // Build TLAS.
        vk::BufferDeviceAddressInfo instance_addr_info{};
        instance_addr_info.buffer = instance_buffer.buffer();

        vk::AccelerationStructureGeometryInstancesDataKHR instances_data{};
        instances_data.arrayOfPointers = vk::False;
        instances_data.data.deviceAddress = device.get().getBufferAddress(instance_addr_info);

        vk::AccelerationStructureGeometryKHR tlas_geometry{};
        tlas_geometry.geometryType = vk::GeometryTypeKHR::eInstances;
        tlas_geometry.geometry.instances = instances_data;

        vk::AccelerationStructureBuildGeometryInfoKHR tlas_build_info{};
        tlas_build_info.type = vk::AccelerationStructureTypeKHR::eTopLevel;
        tlas_build_info.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
        tlas_build_info.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        tlas_build_info.geometryCount = 1;
        tlas_build_info.setGeometries(tlas_geometry);

        vk::AccelerationStructureBuildSizesInfoKHR tlas_build_sizes{
            device.get().getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, tlas_build_info, {total_objects})};

        AllocatedBuffer tlas_buffer{allocator.createBuffer(tlas_build_sizes.accelerationStructureSize,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress), 0,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        vk::AccelerationStructureCreateInfoKHR tlas_create_info{};
        tlas_create_info.buffer = tlas_buffer.buffer();
        tlas_create_info.size = tlas_build_sizes.accelerationStructureSize;
        tlas_create_info.type = vk::AccelerationStructureTypeKHR::eTopLevel;
        vk::raii::AccelerationStructureKHR tlas{device.get(), tlas_create_info};

        tlas_build_info.dstAccelerationStructure = *tlas;

        vk::AccelerationStructureBuildRangeInfoKHR tlas_range_info{};
        tlas_range_info.primitiveCount = total_objects;

        // Build TLAS — scratch buffer scoped so it's freed after build completes.
        {
            uint32_t tlas_scratch_align{device.asScratchAlignment()};
            AllocatedBuffer tlas_scratch{allocator.createBuffer(tlas_build_sizes.buildScratchSize + tlas_scratch_align,
                static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress), 0,
                VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

            vk::BufferDeviceAddressInfo tlas_scratch_addr_info{};
            tlas_scratch_addr_info.buffer = tlas_scratch.buffer();
            vk::DeviceAddress tlas_scratch_addr{device.get().getBufferAddress(tlas_scratch_addr_info)};
            tlas_build_info.scratchData.deviceAddress = (tlas_scratch_addr + tlas_scratch_align - 1) & ~(static_cast<vk::DeviceAddress>(tlas_scratch_align) - 1);

            vk::CommandBufferAllocateInfo build_alloc{};
            build_alloc.commandPool = *command_pool;
            build_alloc.level = vk::CommandBufferLevel::ePrimary;
            build_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers build_cmds(device.get(), build_alloc);

            const vk::raii::CommandBuffer& build_cmd = build_cmds[0];
            vk::CommandBufferBeginInfo build_begin{};
            build_begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            build_cmd.begin(build_begin);

            const vk::AccelerationStructureBuildRangeInfoKHR* tlas_range_ptr{&tlas_range_info};
            build_cmd.buildAccelerationStructuresKHR({tlas_build_info}, {tlas_range_ptr});

            // Barrier: TLAS build write → fragment shader read.
            vk::MemoryBarrier2 tlas_barrier{};
            tlas_barrier.srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
            tlas_barrier.srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
            tlas_barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
            tlas_barrier.dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR;

            vk::DependencyInfo tlas_dep_info{};
            tlas_dep_info.setMemoryBarriers(tlas_barrier);
            build_cmd.pipelineBarrier2(tlas_dep_info);

            build_cmd.end();

            vk::CommandBuffer raw_cmd{*build_cmd};
            vk::SubmitInfo build_submit{};
            build_submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({build_submit});
            device.graphicsQueue().waitIdle();
        }

        logger.logInfo("TLAS built: " + std::to_string(total_objects) + " instances, " + std::to_string(tlas_build_sizes.accelerationStructureSize) + " bytes.");

        // Material definitions — per-object PBR properties in the material SSBO.
        std::vector<Material> material_data{
            {
                // Material 0: obsidian terrain.
                {0.005f, 0.005f, 0.01f}, // base_colour — deep black with cool tint.
                0.15f, // roughness — polished obsidian (slightly glossy to soften terrace reflections).
                {}, // emissive — none.
                0.0f, // emissive_strength.
                0.0f, // metallic.
                1.5f, // ior.
                1.0f, // opacity.
                0.0f, // pad.
            },
            {
                // Material 1: emissive orb.
                {}, // base_colour — not used (pure emissive).
                1.0f, // roughness.
                {1.0f, 0.03f, 0.0f}, // emissive — orange.
                20.0f, // emissive_strength.
                0.0f, // metallic.
                1.5f, // ior.
                1.0f, // opacity.
                0.0f, // pad.
            },
            {
                // Material 2: cyan neon tube.
                {}, // base_colour — not used (pure emissive).
                1.0f, // roughness.
                {0.0f, 0.8f, 1.0f}, // emissive — cyan.
                15.0f, // emissive_strength.
                0.0f, // metallic.
                1.5f, // ior.
                1.0f, // opacity.
                0.0f, // pad.
            },
            {
                // Material 3: orange neon tube.
                {}, // base_colour — not used (pure emissive).
                1.0f, // roughness.
                {1.0f, 0.03f, 0.0f}, // emissive — orange.
                15.0f, // emissive_strength.
                0.0f, // metallic.
                1.5f, // ior.
                1.0f, // opacity.
                0.0f, // pad.
            },
            {
                // Material 4: glass tower (Phase 8 Etape 40 transparent geometry).
                // Opacity < 1 routes the entity through the transparent pipeline (sub-etape
                // 40b) which uses fragTransparent + premultiplied-alpha blending. 0.18 reads
                // as faintly tinted clear glass — not so opaque that the terrain behind
                // disappears, not so transparent the surface vanishes.
                {0.4f, 0.6f, 0.7f}, // base_colour — cool cyan-tinted dielectric.
                0.05f, // roughness — clear glass.
                {}, // emissive — none.
                0.0f, // emissive_strength.
                0.0f, // metallic — pure dielectric.
                1.5f, // ior — typical glass (Schlick F0 ≈ 0.04).
                0.18f, // opacity — faint cyan tint, terrain visible behind.
                0.0f, // pad.
            },
            {
                // Material 5: energy-barrier pillar (Phase 8 Etape 40 transparent geometry).
                // 0.45 lets the cyan-grid floor and any background bleed through while the
                // red emissive still reads as a hot column. Emissive colour matches the orb
                // (material 1) so the two warm light sources visually rhyme. Emissive
                // radiance stays moderate (1.5×) — strong enough to glow against the dark
                // sky, dim enough that ray-traced refraction through the box still shows
                // the cyan grid bent through the interior.
                {0.6f, 0.05f, 0.0f}, // base_colour — deep red.
                0.4f, // roughness — slightly diffused.
                {1.0f, 0.03f, 0.0f}, // emissive — pure red (matches orb material 1).
                1.5f, // emissive_strength.
                0.0f, // metallic.
                1.33f, // ior — soft "energy field" (water-like).
                0.45f, // opacity — semi-transparent energy field.
                0.0f, // pad.
            },
        };

        // Per-object data — meshlet offsets and material index.
        struct MeshInfo {
            uint32_t meshlet_offset{0};
            uint32_t meshlet_count{0};
            uint32_t material_index{0};
        };
        std::vector<MeshInfo> mesh_infos{
            {terrain_meshlet_offset, terrain_meshlet_count, 0},
            {sphere_meshlet_offset, sphere_meshlet_count, 1},
            {cyan_meshlet_offset, cyan_meshlet_count, 2},
            {orange_meshlet_offset, orange_meshlet_count, 3},
            {glass_meshlet_offset, glass_meshlet_count, 4},
            {pillar_meshlet_offset, pillar_meshlet_count, 5},
        };

        std::vector<ObjectData> object_data;
        object_data.reserve(total_objects);
        for (uint32_t i{0}; i < total_objects; ++i) {
            uint32_t mesh_id{scene.meshIDs()[i].id};
            ObjectData obj{};
            obj.model = scene.transforms()[i].modelMatrix();
            obj.meshlet_offset = mesh_infos[mesh_id].meshlet_offset;
            obj.meshlet_count = mesh_infos[mesh_id].meshlet_count;
            obj.material_index = mesh_infos[mesh_id].material_index;
            object_data.push_back(obj);
        }

        // Partition entities into opaque (material.opacity == 1) and transparent (opacity < 1)
        // ranges. Phase 8 Etape 40 expects opaque entities first in the SSBO so that the
        // task shader's two dispatches — opaque (base = 0, count = N_opaque) followed by
        // transparent (base = N_opaque, count = N_transparent) — index a contiguous range
        // each. Verified at startup; future scene-construction changes that violate the
        // invariant will trip the abort below before the GPU sees inconsistent state.
        uint32_t opaque_object_count{0};
        uint32_t transparent_object_count{0};
        for (uint32_t i{0}; i < total_objects; ++i) {
            float opacity{material_data[object_data[i].material_index].opacity};
            if (opacity >= 1.0f) {
                ++opaque_object_count;
            } else {
                ++transparent_object_count;
            }
        }
        for (uint32_t i{0}; i < opaque_object_count; ++i) {
            float opacity{material_data[object_data[i].material_index].opacity};
            if (opacity < 1.0f) {
                logger.logFatal("Entity ordering invariant violated: transparent entity (material " + std::to_string(object_data[i].material_index) + ", opacity "
                    + std::to_string(opacity) + ") found before all opaque entities. Reorder the scene so opaque entities come first.");
                std::abort();
            }
        }
        logger.logInfo("Object partition: " + std::to_string(opaque_object_count) + " opaque, " + std::to_string(transparent_object_count) + " transparent.");

        VkDeviceSize ssbo_size{static_cast<VkDeviceSize>(object_data.size() * sizeof(ObjectData))};
        AllocatedBuffer object_ssbo{allocator.createBuffer(ssbo_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst), 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        // Staging upload for SSBO
        {
            AllocatedBuffer ssbo_staging{allocator.createBuffer(ssbo_size, static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc),
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO)};
            VmaAllocationInfo ssbo_staging_info{ssbo_staging.allocationInfo()};
            std::memcpy(ssbo_staging_info.pMappedData, object_data.data(), ssbo_size);

            vk::CommandBufferAllocateInfo copy_alloc{};
            copy_alloc.commandPool = *command_pool;
            copy_alloc.level = vk::CommandBufferLevel::ePrimary;
            copy_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers copy_cmds(device.get(), copy_alloc);

            const vk::raii::CommandBuffer& copy_cmd = copy_cmds[0];
            vk::CommandBufferBeginInfo copy_begin{};
            copy_begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            copy_cmd.begin(copy_begin);

            vk::BufferCopy ssbo_region{};
            ssbo_region.size = ssbo_size;
            copy_cmd.copyBuffer(ssbo_staging.buffer(), object_ssbo.buffer(), {ssbo_region});

            copy_cmd.end();

            vk::CommandBuffer raw_cmd{*copy_cmd};
            vk::SubmitInfo copy_submit{};
            copy_submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({copy_submit});
            device.graphicsQueue().waitIdle();
        }

        logger.logInfo("Object SSBO uploaded (" + std::to_string(total_objects) + " objects, " + std::to_string(ssbo_size) + " bytes).");

        // Material SSBO — per-object PBR properties.
        VkDeviceSize material_ssbo_size{static_cast<VkDeviceSize>(material_data.size() * sizeof(Material))};
        AllocatedBuffer material_ssbo{allocator.createBuffer(material_ssbo_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst), 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        {
            AllocatedBuffer mat_staging{allocator.createBuffer(material_ssbo_size, static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc),
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO)};
            VmaAllocationInfo mat_staging_info{mat_staging.allocationInfo()};
            std::memcpy(mat_staging_info.pMappedData, material_data.data(), material_ssbo_size);

            vk::CommandBufferAllocateInfo copy_alloc{};
            copy_alloc.commandPool = *command_pool;
            copy_alloc.level = vk::CommandBufferLevel::ePrimary;
            copy_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers copy_cmds(device.get(), copy_alloc);

            const vk::raii::CommandBuffer& copy_cmd = copy_cmds[0];
            vk::CommandBufferBeginInfo copy_begin{};
            copy_begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            copy_cmd.begin(copy_begin);

            vk::BufferCopy mat_region{};
            mat_region.size = material_ssbo_size;
            copy_cmd.copyBuffer(mat_staging.buffer(), material_ssbo.buffer(), {mat_region});

            copy_cmd.end();

            vk::CommandBuffer raw_cmd{*copy_cmd};
            vk::SubmitInfo copy_submit{};
            copy_submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({copy_submit});
            device.graphicsQueue().waitIdle();
        }

        logger.logInfo("Material SSBO uploaded (" + std::to_string(material_data.size()) + " materials, " + std::to_string(material_ssbo_size) + " bytes).");

        // Object bounds SSBO — build from scene bounds
        std::vector<ObjectBounds> object_bounds;
        object_bounds.reserve(total_objects);
        for (const Bounds& b : scene.bounds()) {
            object_bounds.push_back({b.centre, b.radius});
        }

        VkDeviceSize bounds_size{static_cast<VkDeviceSize>(object_bounds.size() * sizeof(ObjectBounds))};
        AllocatedBuffer bounds_ssbo{allocator.createBuffer(bounds_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst), 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        // ── Combined meshlet SSBOs ──

        // Combine meshlet data from terrain + sphere + cyan neon + orange neon + glass tower + energy pillar.
        std::vector<Meshlet> all_meshlets;
        all_meshlets.insert(all_meshlets.end(), terrain_meshlets.meshlets.begin(), terrain_meshlets.meshlets.end());
        all_meshlets.insert(all_meshlets.end(), sphere_meshlets.meshlets.begin(), sphere_meshlets.meshlets.end());
        all_meshlets.insert(all_meshlets.end(), cyan_meshlets.meshlets.begin(), cyan_meshlets.meshlets.end());
        all_meshlets.insert(all_meshlets.end(), orange_meshlets.meshlets.begin(), orange_meshlets.meshlets.end());
        all_meshlets.insert(all_meshlets.end(), glass_meshlets.meshlets.begin(), glass_meshlets.meshlets.end());
        all_meshlets.insert(all_meshlets.end(), pillar_meshlets.meshlets.begin(), pillar_meshlets.meshlets.end());

        std::vector<uint32_t> all_vert_indices;
        all_vert_indices.insert(all_vert_indices.end(), terrain_meshlets.vertex_indices.begin(), terrain_meshlets.vertex_indices.end());
        all_vert_indices.insert(all_vert_indices.end(), sphere_meshlets.vertex_indices.begin(), sphere_meshlets.vertex_indices.end());
        all_vert_indices.insert(all_vert_indices.end(), cyan_meshlets.vertex_indices.begin(), cyan_meshlets.vertex_indices.end());
        all_vert_indices.insert(all_vert_indices.end(), orange_meshlets.vertex_indices.begin(), orange_meshlets.vertex_indices.end());
        all_vert_indices.insert(all_vert_indices.end(), glass_meshlets.vertex_indices.begin(), glass_meshlets.vertex_indices.end());
        all_vert_indices.insert(all_vert_indices.end(), pillar_meshlets.vertex_indices.begin(), pillar_meshlets.vertex_indices.end());

        std::vector<uint8_t> all_tri_indices;
        all_tri_indices.insert(all_tri_indices.end(), terrain_meshlets.triangle_indices.begin(), terrain_meshlets.triangle_indices.end());
        all_tri_indices.insert(all_tri_indices.end(), sphere_meshlets.triangle_indices.begin(), sphere_meshlets.triangle_indices.end());
        all_tri_indices.insert(all_tri_indices.end(), cyan_meshlets.triangle_indices.begin(), cyan_meshlets.triangle_indices.end());
        all_tri_indices.insert(all_tri_indices.end(), orange_meshlets.triangle_indices.begin(), orange_meshlets.triangle_indices.end());
        all_tri_indices.insert(all_tri_indices.end(), glass_meshlets.triangle_indices.begin(), glass_meshlets.triangle_indices.end());
        all_tri_indices.insert(all_tri_indices.end(), pillar_meshlets.triangle_indices.begin(), pillar_meshlets.triangle_indices.end());

        VkDeviceSize meshlet_desc_size{static_cast<VkDeviceSize>(all_meshlets.size() * sizeof(Meshlet))};
        AllocatedBuffer meshlet_desc_ssbo{allocator.createBuffer(meshlet_desc_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst), 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        VkDeviceSize meshlet_vert_idx_size{static_cast<VkDeviceSize>(all_vert_indices.size() * sizeof(uint32_t))};
        AllocatedBuffer meshlet_vert_idx_ssbo{allocator.createBuffer(meshlet_vert_idx_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst), 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        VkDeviceSize meshlet_tri_idx_size{static_cast<VkDeviceSize>(all_tri_indices.size() * sizeof(uint8_t))};
        AllocatedBuffer meshlet_tri_idx_ssbo{allocator.createBuffer(meshlet_tri_idx_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst), 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        // Upload bounds + meshlet data via staging
        {
            AllocatedBuffer bounds_staging{allocator.createBuffer(bounds_size, static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc),
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO)};
            AllocatedBuffer desc_staging{allocator.createBuffer(meshlet_desc_size, static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc),
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO)};
            AllocatedBuffer vert_idx_staging{allocator.createBuffer(meshlet_vert_idx_size, static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc),
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO)};
            AllocatedBuffer tri_idx_staging{allocator.createBuffer(meshlet_tri_idx_size, static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc),
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO)};

            std::memcpy(bounds_staging.allocationInfo().pMappedData, object_bounds.data(), bounds_size);
            std::memcpy(desc_staging.allocationInfo().pMappedData, all_meshlets.data(), meshlet_desc_size);
            std::memcpy(vert_idx_staging.allocationInfo().pMappedData, all_vert_indices.data(), meshlet_vert_idx_size);
            std::memcpy(tri_idx_staging.allocationInfo().pMappedData, all_tri_indices.data(), meshlet_tri_idx_size);

            vk::CommandBufferAllocateInfo copy_alloc{};
            copy_alloc.commandPool = *command_pool;
            copy_alloc.level = vk::CommandBufferLevel::ePrimary;
            copy_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers copy_cmds(device.get(), copy_alloc);

            const vk::raii::CommandBuffer& copy_cmd = copy_cmds[0];
            vk::CommandBufferBeginInfo copy_begin{};
            copy_begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            copy_cmd.begin(copy_begin);

            vk::BufferCopy bounds_region{};
            bounds_region.size = bounds_size;
            copy_cmd.copyBuffer(bounds_staging.buffer(), bounds_ssbo.buffer(), {bounds_region});

            vk::BufferCopy desc_region{};
            desc_region.size = meshlet_desc_size;
            copy_cmd.copyBuffer(desc_staging.buffer(), meshlet_desc_ssbo.buffer(), {desc_region});

            vk::BufferCopy vert_idx_region{};
            vert_idx_region.size = meshlet_vert_idx_size;
            copy_cmd.copyBuffer(vert_idx_staging.buffer(), meshlet_vert_idx_ssbo.buffer(), {vert_idx_region});

            vk::BufferCopy tri_idx_region{};
            tri_idx_region.size = meshlet_tri_idx_size;
            copy_cmd.copyBuffer(tri_idx_staging.buffer(), meshlet_tri_idx_ssbo.buffer(), {tri_idx_region});

            copy_cmd.end();

            vk::CommandBuffer raw_cmd{*copy_cmd};
            vk::SubmitInfo copy_submit{};
            copy_submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({copy_submit});
            device.graphicsQueue().waitIdle();
        }

        logger.logInfo("Bounds + meshlet SSBOs uploaded.");

        // ── Emissive triangle list (for area light sampling) ──

        // Collect all emissive triangles from neon tubes and orb.
        std::vector<EmissiveTriangle> emissive_list;

        // Helper: adds all triangles from a sub-mesh with given emissive radiance.
        auto addEmissiveTriangles = [&emissive_list](const Mesh& mesh, const MathLib::Vec3& emissive_colour, float emissive_strength) {
            MathLib::Vec3 emissive_radiance{emissive_colour.x * emissive_strength, emissive_colour.y * emissive_strength, emissive_colour.z * emissive_strength};
            for (size_t t{0}; t < mesh.indices.size(); t += 3) {
                MathLib::Vec3 v0{mesh.positions[mesh.indices[t + 0]]};
                MathLib::Vec3 v1{mesh.positions[mesh.indices[t + 1]]};
                MathLib::Vec3 v2{mesh.positions[mesh.indices[t + 2]]};

                MathLib::Vec3 edge1{v1 - v0};
                MathLib::Vec3 edge2{v2 - v0};
                float area{edge1.cross(edge2).length() * 0.5f};

                EmissiveTriangle etri{};
                etri.v0 = v0;
                etri.v1 = v1;
                etri.v2 = v2;
                etri.emissive = emissive_radiance;
                etri.area = area;
                emissive_list.push_back(etri);
            }
        };

        // Cyan neon tubes.
        addEmissiveTriangles(neon_tubes.cyan, {0.0f, 0.8f, 1.0f}, 15.0f);
        // Orange neon tubes.
        addEmissiveTriangles(neon_tubes.orange, {1.0f, 0.03f, 0.0f}, 15.0f);
        // Energy-barrier pillar — modest emissive so the terrain picks it up as a soft red key
        // light (same emissive colour × strength as material 5 so ReSTIR estimates and the
        // material SSBO self-illumination agree numerically). Colour matches the orb so the
        // pillar reads as a "lower sister" of the sun-orb light.
        addEmissiveTriangles(energy_pillar, {1.0f, 0.03f, 0.0f}, 1.5f);

        // Orb emissive triangles.
        {
            MathLib::Vec3 orb_emissive{1.0f * 20.0f, 0.03f * 20.0f, 0.0f};
            for (size_t t{0}; t < sphere_indices.size(); t += 3) {
                MathLib::Vec3 v0{sphere_positions[sphere_indices[t + 0]] + orb_pos};
                MathLib::Vec3 v1{sphere_positions[sphere_indices[t + 1]] + orb_pos};
                MathLib::Vec3 v2{sphere_positions[sphere_indices[t + 2]] + orb_pos};

                MathLib::Vec3 edge1{v1 - v0};
                MathLib::Vec3 edge2{v2 - v0};
                float area{edge1.cross(edge2).length() * 0.5f};

                EmissiveTriangle etri{};
                etri.v0 = v0;
                etri.v1 = v1;
                etri.v2 = v2;
                etri.emissive = orb_emissive;
                etri.area = area;
                emissive_list.push_back(etri);
            }
        }

        // Build power-weighted CDF.
        float total_power{0.0f};
        for (EmissiveTriangle& etri : emissive_list) {
            float lum{0.2126f * etri.emissive.x + 0.7152f * etri.emissive.y + 0.0722f * etri.emissive.z};
            total_power += etri.area * lum;
        }

        float running_cdf{0.0f};
        for (EmissiveTriangle& etri : emissive_list) {
            float lum{0.2126f * etri.emissive.x + 0.7152f * etri.emissive.y + 0.0722f * etri.emissive.z};
            running_cdf += etri.area * lum / total_power;
            etri.cdf = running_cdf;
        }
        // Ensure last CDF value is exactly 1.0.
        if (!emissive_list.empty()) {
            emissive_list.back().cdf = 1.0f;
        }

        uint32_t emissive_count{static_cast<uint32_t>(emissive_list.size())};
        logger.logInfo("Emissive triangle list: " + std::to_string(emissive_count) + " triangles, total power " + std::to_string(total_power) + ".");

        // Upload emissive SSBO.
        VkDeviceSize emissive_ssbo_size{static_cast<VkDeviceSize>(emissive_list.size() * sizeof(EmissiveTriangle))};
        AllocatedBuffer emissive_ssbo{allocator.createBuffer(emissive_ssbo_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst), 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        {
            AllocatedBuffer emissive_staging{allocator.createBuffer(emissive_ssbo_size, static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc),
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO)};
            std::memcpy(emissive_staging.allocationInfo().pMappedData, emissive_list.data(), emissive_ssbo_size);

            vk::CommandBufferAllocateInfo copy_alloc{};
            copy_alloc.commandPool = *command_pool;
            copy_alloc.level = vk::CommandBufferLevel::ePrimary;
            copy_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers copy_cmds(device.get(), copy_alloc);

            const vk::raii::CommandBuffer& copy_cmd = copy_cmds[0];
            vk::CommandBufferBeginInfo copy_begin{};
            copy_begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            copy_cmd.begin(copy_begin);

            vk::BufferCopy region{};
            region.size = emissive_ssbo_size;
            copy_cmd.copyBuffer(emissive_staging.buffer(), emissive_ssbo.buffer(), {region});

            copy_cmd.end();

            vk::CommandBuffer raw_cmd{*copy_cmd};
            vk::SubmitInfo copy_submit{};
            copy_submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({copy_submit});
            device.graphicsQueue().waitIdle();
        }

        logger.logInfo("Emissive SSBO uploaded (" + std::to_string(emissive_ssbo_size) + " bytes).");

        // ── Reservoir ping-pong buffers for ReSTIR temporal reuse ──

        // The ping-pong binding pattern below requires exactly two in-flight frames
        // (frame 0 writes A/reads B, frame 1 writes B/reads A). Any other value
        // would map multiple frames to the same reservoir buffer, producing a
        // genuine data race on the fragment-shader storage writes.
        static_assert(MAX_FRAMES_IN_FLIGHT == 2, "Reservoir ping-pong requires exactly 2 frames in flight");

        // Size for up to 4K resolution — any larger window will overflow the buffer.
        // resizeReservoirsForExtent (below) asserts this on every resize.
        constexpr uint32_t RESERVOIR_MAX_WIDTH{3840};
        constexpr uint32_t RESERVOIR_MAX_HEIGHT{2160};
        VkDeviceSize reservoir_buffer_size{static_cast<VkDeviceSize>(RESERVOIR_MAX_WIDTH) * RESERVOIR_MAX_HEIGHT * sizeof(Reservoir)};

        AllocatedBuffer reservoir_a{allocator.createBuffer(reservoir_buffer_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst), 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        AllocatedBuffer reservoir_b{allocator.createBuffer(reservoir_buffer_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst), 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        // Zero-initialise both reservoir buffers to prevent garbage data on the first frame.
        {
            vk::CommandBufferAllocateInfo cmd_alloc{};
            cmd_alloc.commandPool = *command_pool;
            cmd_alloc.level = vk::CommandBufferLevel::ePrimary;
            cmd_alloc.commandBufferCount = 1;
            vk::raii::CommandBuffers cmds(device.get(), cmd_alloc);

            const vk::raii::CommandBuffer& cmd = cmds[0];
            vk::CommandBufferBeginInfo begin_info{};
            begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            cmd.begin(begin_info);
            cmd.fillBuffer(reservoir_a.buffer(), 0, reservoir_buffer_size, 0);
            cmd.fillBuffer(reservoir_b.buffer(), 0, reservoir_buffer_size, 0);
            cmd.end();

            vk::CommandBuffer raw_cmd{*cmd};
            vk::SubmitInfo submit{};
            submit.setCommandBuffers(raw_cmd);
            device.graphicsQueue().submit({submit});
            device.graphicsQueue().waitIdle();
        }

        logger.logInfo("Reservoir buffers allocated and zeroed: 2 × " + std::to_string(reservoir_buffer_size) + " bytes (" + std::to_string(RESERVOIR_MAX_WIDTH) + "×"
            + std::to_string(RESERVOIR_MAX_HEIGHT) + " pixels).");

        // Bind all descriptor sets — SSBOs + TLAS are static, UBOs are per-frame.
        // Reservoir ping-pong: frame 0 writes A / reads B, frame 1 writes B / reads A.
        for (uint32_t i{0}; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            pipeline.bindSSBOs(i, object_ssbo.buffer(), ssbo_size, bounds_ssbo.buffer(), bounds_size, meshlet_desc_ssbo.buffer(), meshlet_desc_size,
                vertex_buffer.buffer(), vertex_buffer_size, meshlet_vert_idx_ssbo.buffer(), meshlet_vert_idx_size, meshlet_tri_idx_ssbo.buffer(), meshlet_tri_idx_size);
            pipeline.bindTLAS(i, *tlas);
            pipeline.bindMaterialSSBO(i, material_ssbo.buffer(), material_ssbo_size);
            pipeline.bindEmissiveSSBO(i, emissive_ssbo.buffer(), emissive_ssbo_size);

            VkBuffer write_buf{(i == 0) ? reservoir_a.buffer() : reservoir_b.buffer()};
            VkBuffer read_buf{(i == 0) ? reservoir_b.buffer() : reservoir_a.buffer()};
            pipeline.bindReservoirBuffers(i, write_buf, reservoir_buffer_size, read_buf, reservoir_buffer_size);
        }

        // Render event signal — main thread emits, render thread consumes
        SignalsLib::Signal<RenderEvent> render_signal;
        std::mutex render_mutex;
        std::condition_variable render_cv;

        // Spawn the render thread
        std::thread render_worker(renderThread, std::ref(device), std::ref(swapchain), std::ref(pipeline), std::ref(allocator), opaque_object_count,
            transparent_object_count, emissive_count, total_power, std::ref(command_pool), std::ref(command_buffers), reservoir_a.buffer(), reservoir_b.buffer(),
            reservoir_buffer_size, std::ref(render_signal), std::ref(render_mutex), std::ref(render_cv), std::ref(logger));

        //! Emits a render event and wakes the render thread.
        auto emitRenderEvent = [&](RenderEvent event) {
            render_signal.emit(event);
            render_cv.notify_one();
        };

        // Immediate event callback — fires from wndProc/handleEvent, including during
        // Win32 modal resize loops when the main event loop is blocked. Forwards all
        // visual and input events to the render thread.
        struct CallbackContext {
            SignalsLib::Signal<RenderEvent>* signal;
            std::condition_variable* cv;
            WindowLib::Window* window;
        };

        CallbackContext cb_ctx{&render_signal, &render_cv, window.get()};

        window->setEventCallback(
            [](const WindowLib::WindowEvent& ev, void* user_data) {
                CallbackContext* ctx = static_cast<CallbackContext*>(user_data);
                RenderEvent re{};
                switch (ev.type) {
                case WindowLib::WindowEvent::Type::Resize:
                    re.type = RenderEvent::Type::Resize;
                    re.width = ev.resize.width;
                    re.height = ev.resize.height;
                    break;
                case WindowLib::WindowEvent::Type::Expose:
                    re.type = RenderEvent::Type::Render;
                    break;
                case WindowLib::WindowEvent::Type::KeyDown:
                    re.type = RenderEvent::Type::KeyDown;
                    re.width = ev.key.keycode;
                    break;
                case WindowLib::WindowEvent::Type::KeyUp:
                    re.type = RenderEvent::Type::KeyUp;
                    re.width = ev.key.keycode;
                    break;
                case WindowLib::WindowEvent::Type::MouseMove:
                    re.type = RenderEvent::Type::MouseMove;
                    re.dx = ev.mouse_move.dx;
                    re.dy = ev.mouse_move.dy;
                    break;
                case WindowLib::WindowEvent::Type::MouseButtonDown:
                    re.type = RenderEvent::Type::MouseButton;
                    re.width = ev.mouse_button.button;
                    re.pressed = true;
                    // Toggle cursor capture on right-click (must be on main thread)
                    if (ev.mouse_button.button == MOUSE_RIGHT) {
                        ctx->window->setCursorCaptured(!ctx->window->cursorCaptured());
                    }
                    break;
                case WindowLib::WindowEvent::Type::MouseButtonUp:
                    re.type = RenderEvent::Type::MouseButton;
                    re.width = ev.mouse_button.button;
                    re.pressed = false;
                    break;
                case WindowLib::WindowEvent::Type::Close:
                    re.type = RenderEvent::Type::Stop;
                    break;
                default:
                    return; // Don't emit for unhandled events.
                }
                ctx->signal->emit(re);
                ctx->cv->notify_one();
            },
            &cb_ctx);

        // Request initial frame render
        emitRenderEvent({RenderEvent::Type::Render, 0, 0, 0, 0, false});

        logger.logInfo("Press ESC to close. Right-click to toggle mouse look. WASD + Space/Shift to fly.");

        // Main loop — handles Close and ESC. All other events are forwarded
        // to the render thread via the immediate callback above.
        while (!window->shouldClose()) {
            window->waitEvents();

            WindowLib::WindowEvent ev;
            while (window->pollEvent(ev)) {
                switch (ev.type) {
                case WindowLib::WindowEvent::Type::Close:
                    logger.logInfo("Close requested.");
                    break;

                case WindowLib::WindowEvent::Type::KeyDown:
                    if (ev.key.keycode == KEY_ESC) {
                        if (window->cursorCaptured()) {
                            window->setCursorCaptured(false);
                        } else {
                            window->requestClose();
                        }
                    }
                    break;

                default:
                    break;
                }
            }
        }

        // Signal render thread to stop and wait for it.
        // The render thread waits for in-flight fences + present queue before returning.
        emitRenderEvent({RenderEvent::Type::Stop, 0, 0, 0, 0, false});
        render_worker.join();

        // Clear the event callback before cb_ctx goes out of scope — prevents the
        // window from invoking a dangling pointer during its own destruction.
        window->setEventCallback(nullptr, nullptr);

        logger.logInfo("Shutting down.");

    } catch (const vk::SystemError& e) {
        logger.logFatal(std::string("Vulkan error: ") + e.what());
        return 1;
    } catch (const std::exception& e) {
        logger.logFatal(std::string("Fatal error: ") + e.what());
        return 1;
    }

    return 0;
}
