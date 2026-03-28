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
#include <array>
#include <chrono>
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

//! Camera movement speed (metres per second).
constexpr float CAMERA_SPEED = 5.0f;

//! Camera mouse sensitivity (radians per pixel).
constexpr float MOUSE_SENSITIVITY = 0.003f;

//! Platform-specific key codes for WASD, Space, Shift, and right mouse button.
#ifdef _WIN32
constexpr uint32_t KEY_W = 0x57;
constexpr uint32_t KEY_A = 0x41;
constexpr uint32_t KEY_S = 0x53;
constexpr uint32_t KEY_D = 0x44;
constexpr uint32_t KEY_SPACE = 0x20;
constexpr uint32_t KEY_SHIFT = 0x10;
constexpr uint32_t KEY_ESC = 27;
constexpr uint32_t MOUSE_RIGHT = 1;
#else
constexpr uint32_t KEY_W = 25;
constexpr uint32_t KEY_A = 38;
constexpr uint32_t KEY_S = 39;
constexpr uint32_t KEY_D = 40;
constexpr uint32_t KEY_SPACE = 65;
constexpr uint32_t KEY_SHIFT = 50;
constexpr uint32_t KEY_ESC = 9;
constexpr uint32_t MOUSE_RIGHT = 2;
#endif

/*!
    Records a command buffer with mesh shader rendering. The task shader performs
    per-object frustum culling and dispatches mesh shader workgroups for visible objects.
*/
static void recordFrame(const vk::raii::CommandBuffer& cmd, vk::Image image, vk::ImageView colour_view, vk::ImageView depth_view, vk::Image depth_image,
    vk::Extent2D extent, const Pipeline& pipeline, vk::DescriptorSet descriptor_set, const MathLib::Frustum& frustum, uint32_t total_objects)
{
    vk::CommandBufferBeginInfo begin_info{};
    begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd.begin(begin_info);

    // Cross-frame synchronisation for depth image.
    vk::MemoryBarrier2 cross_frame{};
    cross_frame.srcStageMask = vk::PipelineStageFlagBits2::eLateFragmentTests;
    cross_frame.srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    cross_frame.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests;
    cross_frame.dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;

    vk::DependencyInfo cross_frame_dep{};
    cross_frame_dep.memoryBarrierCount = 1;
    cross_frame_dep.pMemoryBarriers = &cross_frame;
    cmd.pipelineBarrier2(cross_frame_dep);

    // Transition colour: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL.
    vk::ImageMemoryBarrier2 to_colour{};
    to_colour.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    to_colour.srcAccessMask = vk::AccessFlagBits2::eNone;
    to_colour.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    to_colour.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
    to_colour.oldLayout = vk::ImageLayout::eUndefined;
    to_colour.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
    to_colour.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    to_colour.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    to_colour.image = image;
    to_colour.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    to_colour.subresourceRange.baseMipLevel = 0;
    to_colour.subresourceRange.levelCount = 1;
    to_colour.subresourceRange.baseArrayLayer = 0;
    to_colour.subresourceRange.layerCount = 1;

    // Transition depth: UNDEFINED → DEPTH_ATTACHMENT_OPTIMAL.
    vk::ImageMemoryBarrier2 to_depth{};
    to_depth.srcStageMask = vk::PipelineStageFlagBits2::eLateFragmentTests;
    to_depth.srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    to_depth.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests;
    to_depth.dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    to_depth.oldLayout = vk::ImageLayout::eUndefined;
    to_depth.newLayout = vk::ImageLayout::eDepthAttachmentOptimal;
    to_depth.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    to_depth.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    to_depth.image = depth_image;
    to_depth.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
    to_depth.subresourceRange.baseMipLevel = 0;
    to_depth.subresourceRange.levelCount = 1;
    to_depth.subresourceRange.baseArrayLayer = 0;
    to_depth.subresourceRange.layerCount = 1;

    std::array<vk::ImageMemoryBarrier2, 2> barriers{to_colour, to_depth};
    vk::DependencyInfo dep_to_render{};
    dep_to_render.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dep_to_render.pImageMemoryBarriers = barriers.data();
    cmd.pipelineBarrier2(dep_to_render);

    // Clear colour — dark teal.
    vk::RenderingAttachmentInfo colour_attachment{};
    colour_attachment.imageView = colour_view;
    colour_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    colour_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    colour_attachment.storeOp = vk::AttachmentStoreOp::eStore;
    colour_attachment.clearValue.color = vk::ClearColorValue{std::array{0.01f, 0.01f, 0.02f, 1.0f}};

    // Clear depth to 1.0 (far plane).
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
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &colour_attachment;
    rendering_info.pDepthAttachment = &depth_attachment;

    cmd.beginRendering(rendering_info);

    // Bind mesh shader pipeline and set dynamic state.
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.get());

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

    // Bind descriptor set (all SSBOs + UBO).
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline.layout(), 0, {descriptor_set}, {});

    // Push frustum planes and object/meshlet counts to the task shader.
    TaskPushConstants push{};
    push.planes = frustum.planes;
    push.object_count = total_objects;
    cmd.pushConstants<TaskPushConstants>(*pipeline.layout(), vk::ShaderStageFlagBits::eTaskEXT, 0, push);

    // Dispatch one task shader workgroup per object. The task shader culls
    // and dispatches mesh shader workgroups for visible objects.
    cmd.drawMeshTasksEXT(total_objects, 1, 1);

    cmd.endRendering();

    // Transition: COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC
    vk::ImageMemoryBarrier2 to_present{};
    to_present.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    to_present.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
    to_present.dstStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe;
    to_present.dstAccessMask = vk::AccessFlagBits2::eNone;
    to_present.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
    to_present.newLayout = vk::ImageLayout::ePresentSrcKHR;
    to_present.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    to_present.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    to_present.image = image;
    to_present.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    to_present.subresourceRange.baseMipLevel = 0;
    to_present.subresourceRange.levelCount = 1;
    to_present.subresourceRange.baseArrayLayer = 0;
    to_present.subresourceRange.layerCount = 1;

    vk::DependencyInfo dep_to_present{};
    dep_to_present.imageMemoryBarrierCount = 1;
    dep_to_present.pImageMemoryBarriers = &to_present;
    cmd.pipelineBarrier2(dep_to_present);

    cmd.end();
}

/*!
    Render thread entry point. Blocks on a condition variable until the main thread
    sends RenderEvent messages via Signal<T>. Owns the Vulkan rendering timeline,
    camera state, input processing, and delta time.
*/
static void renderThread(Device& device, Swapchain& swapchain, Pipeline& pipeline, Allocator& allocator, uint32_t total_objects,
    vk::raii::CommandBuffers& command_buffers, SignalsLib::Signal<RenderEvent>& render_signal, std::mutex& render_mutex, std::condition_variable& render_cv,
    LoggingLib::Logger& logger)
{
    // Frame synchronisation — per-frame fences and acquire semaphores
    std::vector<vk::raii::Semaphore> image_available_semaphores;
    std::vector<vk::raii::Fence> in_flight_fences;

    vk::SemaphoreCreateInfo sem_info{};
    vk::FenceCreateInfo fence_info{};
    fence_info.flags = vk::FenceCreateFlagBits::eSignaled;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        image_available_semaphores.push_back(vk::raii::Semaphore(device.get(), sem_info));
        in_flight_fences.push_back(vk::raii::Fence(device.get(), fence_info));
    }

    // Per-swapchain-image semaphores for present
    std::vector<vk::raii::Semaphore> render_finished_semaphores;

    auto rebuildPresentSemaphores = [&]() {
        render_finished_semaphores.clear();
        for (uint32_t i = 0; i < swapchain.imageCount(); ++i) {
            render_finished_semaphores.push_back(vk::raii::Semaphore(device.get(), sem_info));
        }
    };
    rebuildPresentSemaphores();

    // Depth buffer — same dimensions as swapchain, recreated on resize
    AllocatedImage depth_image = allocator.createImage(swapchain.extent().width, swapchain.extent().height, static_cast<VkFormat>(DEPTH_FORMAT),
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

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

    //! Rebuilds the depth buffer to match the current swapchain extent.
    auto rebuildDepthBuffer = [&]() {
        depth_image = allocator.createImage(swapchain.extent().width, swapchain.extent().height, static_cast<VkFormat>(DEPTH_FORMAT),
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        depth_view_info.image = depth_image.image();
        depth_view = vk::raii::ImageView(device.get(), depth_view_info);
    };

    // Per-frame camera UBOs (persistently mapped)
    std::vector<AllocatedBuffer> ubo_buffers;
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        AllocatedBuffer ubo = allocator.createBuffer(sizeof(CameraUBO), static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eUniformBuffer),
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO);
        pipeline.bindUBO(i, ubo.buffer());
        VmaAllocationInfo ubo_info = ubo.allocationInfo();
        pipeline.setUBOMappedPtr(i, ubo_info.pMappedData);
        ubo_buffers.push_back(std::move(ubo));
    }

    // Camera starts above the terrain centre, looking along it.
    Camera camera({0.0f, 8.0f, 35.0f}, MathLib::PI / 4.0f, 0.1f, 200.0f);

    // Input state
    std::unordered_set<uint32_t> keys_held;
    bool mouse_captured{false};

    // Delta time
    std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now();

    uint32_t current_frame{0};

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
                if (ev.width == MOUSE_RIGHT && ev.pressed) {
                    mouse_captured = !mouse_captured;
                    should_render = true;
                }
                break;
            case RenderEvent::Type::Stop:
                should_stop = true;
                break;
            }
        }

        if (should_stop) {
            device.get().waitIdle();
            return;
        }

        // Delta time
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        float delta_time = std::chrono::duration<float>(now - last_time).count();
        last_time = now;

        // Clamp delta time to avoid huge jumps (e.g., after breakpoint or resize stall)
        if (delta_time > 0.1f) {
            delta_time = 0.1f;
        }

        // Process held keys — camera movement
        if (!keys_held.empty()) {
            float move_delta = CAMERA_SPEED * delta_time;
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
            if (resize_width > 0 && resize_height > 0) {
                swapchain.recreate(resize_width, resize_height);
                rebuildPresentSemaphores();
                rebuildDepthBuffer();
            } else {
                should_render = false;
            }
        }

        // Skip rendering while minimised or if no render was requested
        if (!should_render || swapchain.extent().width == 0 || swapchain.extent().height == 0) {
            // If keys are held, request another frame via self-notification
            if (!keys_held.empty()) {
                render_cv.notify_one();
            }
            continue;
        }

        // Wait for this frame's fence
        vk::Result wait_result = device.get().waitForFences({*in_flight_fences[current_frame]}, vk::True, std::numeric_limits<uint64_t>::max());
        if (wait_result != vk::Result::eSuccess) {
            logger.logFatal("Failed to wait for fence.");
            std::abort();
            return;
        }

        // Acquire next swapchain image
        uint32_t image_index{0};
        vk::Result acquire_result = vk::Result::eSuccess;
        try {
            vk::ResultValue<uint32_t> acquire = swapchain.get().acquireNextImage(std::numeric_limits<uint64_t>::max(), *image_available_semaphores[current_frame]);
            acquire_result = acquire.result;
            image_index = acquire.value;
        } catch (const vk::OutOfDateKHRError&) {
            if (swapchain.extent().width > 0 && swapchain.extent().height > 0) {
                swapchain.recreate(swapchain.extent().width, swapchain.extent().height);
                rebuildPresentSemaphores();
                rebuildDepthBuffer();
            }
            continue;
        }

        bool swapchain_suboptimal = (acquire_result == vk::Result::eSuboptimalKHR);

        // Only reset fence after we know we will submit work
        device.get().resetFences({*in_flight_fences[current_frame]});

        // Update camera UBO and extract frustum planes
        MathLib::Frustum frustum{};
        {
            uint32_t extent_height{swapchain.extent().height};
            if (extent_height == 0) {
                continue;
            }
            float aspect{static_cast<float>(swapchain.extent().width) / static_cast<float>(extent_height)};
            CameraUBO ubo{};
            ubo.view = camera.viewMatrix();
            ubo.projection = camera.projectionMatrix(aspect);
            pipeline.updateCameraUBO(current_frame, ubo);
            frustum = MathLib::extractFrustum(ubo.projection * ubo.view);
        }

        // Record command buffer — compute culling + graphics rendering in one submission
        const vk::raii::CommandBuffer& cmd = command_buffers[current_frame];
        cmd.reset();
        recordFrame(cmd, swapchain.images()[image_index], *swapchain.views()[image_index], *depth_view, depth_image.image(), swapchain.extent(), pipeline,
            pipeline.descriptorSet(current_frame), frustum, total_objects);

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
        submit_info.waitSemaphoreInfoCount = 1;
        submit_info.pWaitSemaphoreInfos = &wait_sem;
        submit_info.commandBufferInfoCount = 1;
        submit_info.pCommandBufferInfos = &cmd_submit;
        submit_info.signalSemaphoreInfoCount = 1;
        submit_info.pSignalSemaphoreInfos = &signal_sem;

        device.graphicsQueue().submit2({submit_info}, *in_flight_fences[current_frame]);

        // Present
        vk::PresentInfoKHR present_info{};
        vk::SwapchainKHR swapchains[] = {*swapchain.get()};
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &*render_finished_semaphores[image_index];
        present_info.swapchainCount = 1;
        present_info.pSwapchains = swapchains;
        present_info.pImageIndices = &image_index;

        vk::Result present_result = vk::Result::eSuccess;
        try {
            present_result = device.presentQueue().presentKHR(present_info);
        } catch (const vk::OutOfDateKHRError&) {
            if (swapchain.extent().width > 0 && swapchain.extent().height > 0) {
                swapchain.recreate(swapchain.extent().width, swapchain.extent().height);
                rebuildPresentSemaphores();
                rebuildDepthBuffer();
            }
            continue;
        }

        if (present_result == vk::Result::eSuboptimalKHR || swapchain_suboptimal) {
            if (swapchain.extent().width > 0 && swapchain.extent().height > 0) {
                swapchain.recreate(swapchain.extent().width, swapchain.extent().height);
                rebuildPresentSemaphores();
                rebuildDepthBuffer();
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

        std::unique_ptr<WindowLib::Window> window = WindowLib::create(config, logger);
        logger.logInfo("Window created: " + std::to_string(config.width) + "x" + std::to_string(config.height) + ".");

        // Vulkan initialisation
        constexpr bool ENABLE_VALIDATION =
#ifdef NDEBUG
            false;
#else
            true;
#endif

        Instance instance(ENABLE_VALIDATION, requiredSurfaceExtensions(), logger);
        vk::raii::SurfaceKHR surface = createSurface(instance.get(), *window);
        Device device(instance, *surface, logger);

        logger.logInfo("Vulkan ready - GPU: " + device.name() + ".");

        // VMA allocator
        Allocator allocator(instance, device, logger);

        // Load shaders and create pipelines
        std::string exe_dir = executableDirectory();
        std::vector<uint32_t> task_spirv{loadSpirv(exe_dir + "task.spv", logger)};
        std::vector<uint32_t> mesh_spirv{loadSpirv(exe_dir + "mesh.spv", logger)};

        // Create swapchain
        Swapchain swapchain(device, *surface, config.width, config.height, logger);

        // Mesh shader pipeline (task + mesh + fragment). mesh.spv is a combined Slang
        // module containing both meshMain and fragMain entry points.
        Pipeline pipeline(device, swapchain.format().format, DEPTH_FORMAT, task_spirv, mesh_spirv, MAX_FRAMES_IN_FLIGHT, logger);

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

        // Vertex buffer.
        VkDeviceSize vertex_buffer_size{static_cast<VkDeviceSize>(terrain.vertices.size() * sizeof(Vertex))};
        AllocatedBuffer vertex_buffer{allocator.createBuffer(vertex_buffer_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst), 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        {
            AllocatedBuffer vertex_staging{allocator.createBuffer(vertex_buffer_size, static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc),
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO)};
            std::memcpy(vertex_staging.allocationInfo().pMappedData, terrain.vertices.data(), vertex_buffer_size);

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

            vk::SubmitInfo copy_submit{};
            copy_submit.commandBufferCount = 1;
            vk::CommandBuffer raw_cmd = *copy_cmd;
            copy_submit.pCommandBuffers = &raw_cmd;
            device.graphicsQueue().submit({copy_submit});
            device.graphicsQueue().waitIdle();
        }

        logger.logInfo("Vertex buffer uploaded (" + std::to_string(terrain.vertices.size()) + " vertices).");

        // Scene — single terrain entity at origin.
        Scene scene;
        Transform terrain_transform{};
        (void)scene.addEntity(terrain_transform, {0}, {{0.0f, 0.0f, 0.0f}, terrain.bounding_radius});

        uint32_t total_objects{scene.entityCount()};

        std::vector<ObjectData> object_data;
        object_data.reserve(total_objects);
        for (uint32_t i{0}; i < total_objects; ++i) {
            ObjectData obj{};
            obj.model = scene.transforms()[i].modelMatrix();
            obj.meshlet_offset = terrain_meshlet_offset;
            obj.meshlet_count = terrain_meshlet_count;
            object_data.push_back(obj);
        }

        VkDeviceSize ssbo_size = static_cast<VkDeviceSize>(object_data.size() * sizeof(ObjectData));
        AllocatedBuffer object_ssbo = allocator.createBuffer(ssbo_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst), 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

        // Staging upload for SSBO
        {
            AllocatedBuffer ssbo_staging = allocator.createBuffer(ssbo_size, static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc),
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO);
            VmaAllocationInfo ssbo_staging_info = ssbo_staging.allocationInfo();
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

            vk::SubmitInfo copy_submit{};
            copy_submit.commandBufferCount = 1;
            vk::CommandBuffer raw_cmd = *copy_cmd;
            copy_submit.pCommandBuffers = &raw_cmd;
            device.graphicsQueue().submit({copy_submit});
            device.graphicsQueue().waitIdle();
        }

        logger.logInfo("Object SSBO uploaded (" + std::to_string(total_objects) + " objects, " + std::to_string(ssbo_size) + " bytes).");

        // Object bounds SSBO — build from scene bounds
        std::vector<ObjectBounds> object_bounds;
        object_bounds.reserve(total_objects);
        for (const Bounds& b : scene.bounds()) {
            object_bounds.push_back({b.centre, b.radius});
        }

        VkDeviceSize bounds_size{static_cast<VkDeviceSize>(object_bounds.size() * sizeof(ObjectBounds))};
        AllocatedBuffer bounds_ssbo{allocator.createBuffer(bounds_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst), 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        // Meshlet SSBOs — descriptors, vertex indices, triangle indices
        VkDeviceSize meshlet_desc_size{static_cast<VkDeviceSize>(terrain_meshlets.meshlets.size() * sizeof(Meshlet))};
        AllocatedBuffer meshlet_desc_ssbo{allocator.createBuffer(meshlet_desc_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst), 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        VkDeviceSize meshlet_vert_idx_size{static_cast<VkDeviceSize>(terrain_meshlets.vertex_indices.size() * sizeof(uint32_t))};
        AllocatedBuffer meshlet_vert_idx_ssbo{allocator.createBuffer(meshlet_vert_idx_size,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst), 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)};

        VkDeviceSize meshlet_tri_idx_size{static_cast<VkDeviceSize>(terrain_meshlets.triangle_indices.size() * sizeof(uint8_t))};
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
            std::memcpy(desc_staging.allocationInfo().pMappedData, terrain_meshlets.meshlets.data(), meshlet_desc_size);
            std::memcpy(vert_idx_staging.allocationInfo().pMappedData, terrain_meshlets.vertex_indices.data(), meshlet_vert_idx_size);
            std::memcpy(tri_idx_staging.allocationInfo().pMappedData, terrain_meshlets.triangle_indices.data(), meshlet_tri_idx_size);

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

            vk::SubmitInfo copy_submit{};
            copy_submit.commandBufferCount = 1;
            vk::CommandBuffer raw_cmd = *copy_cmd;
            copy_submit.pCommandBuffers = &raw_cmd;
            device.graphicsQueue().submit({copy_submit});
            device.graphicsQueue().waitIdle();
        }

        logger.logInfo("Bounds + meshlet SSBOs uploaded.");

        // Bind all descriptor sets — SSBOs are static, UBOs are per-frame
        for (uint32_t i{0}; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            pipeline.bindSSBOs(i, object_ssbo.buffer(), ssbo_size, bounds_ssbo.buffer(), bounds_size, meshlet_desc_ssbo.buffer(), meshlet_desc_size,
                vertex_buffer.buffer(), vertex_buffer_size, meshlet_vert_idx_ssbo.buffer(), meshlet_vert_idx_size, meshlet_tri_idx_ssbo.buffer(), meshlet_tri_idx_size);
        }

        // Render event signal — main thread emits, render thread consumes
        SignalsLib::Signal<RenderEvent> render_signal;
        std::mutex render_mutex;
        std::condition_variable render_cv;

        // Spawn the render thread
        std::thread render_worker(renderThread, std::ref(device), std::ref(swapchain), std::ref(pipeline), std::ref(allocator), static_cast<uint32_t>(total_objects),
            std::ref(command_buffers), std::ref(render_signal), std::ref(render_mutex), std::ref(render_cv), std::ref(logger));

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
                default:
                    return; // Don't emit for unhandled events
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

        // Signal render thread to stop and wait for it
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
