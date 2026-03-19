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
#include "device.hpp"
#include "instance.hpp"
#include "pipeline.hpp"
#include "surface.hpp"
#include "swapchain.hpp"
#include <log/logger.hpp>
#include <signal/signal.hpp>
#include <window/window.hpp>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

//! Event sent from the main (event) thread to the render thread via Signal<T>.
struct RenderEvent {
    //! Discriminator for the render event type.
    enum class Type {
        Render, //!< Window needs redrawing (expose, initial frame).
        Resize, //!< Window was resized — recreate swapchain before rendering.
        Stop //!< Render thread should shut down.
    };

    Type type{Type::Render}; //!< Event type.
    uint32_t width{0}; //!< New width (only meaningful for Resize events).
    uint32_t height{0}; //!< New height (only meaningful for Resize events).
};

//! Maximum number of frames that can be in-flight simultaneously.
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

//! Hardcoded triangle vertex data — red, green, blue corners.
constexpr std::array<Vertex, 3> TRIANGLE_VERTICES = {{
    {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
    {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
    {{0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
}};

/*!
    Records a command buffer that clears to dark teal, draws the triangle,
    and transitions the swapchain image for presentation.
*/
static void recordFrame(const vk::raii::CommandBuffer& cmd, vk::Image image, vk::ImageView view, vk::Extent2D extent, const Pipeline& pipeline, VkBuffer vertex_buffer)
{
    vk::CommandBufferBeginInfo begin_info{};
    begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd.begin(begin_info);

    // Transition: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
    vk::ImageMemoryBarrier2 to_colour{};
    to_colour.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
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

    vk::DependencyInfo dep_to_colour{};
    dep_to_colour.imageMemoryBarrierCount = 1;
    dep_to_colour.pImageMemoryBarriers = &to_colour;
    cmd.pipelineBarrier2(dep_to_colour);

    // Clear colour — dark teal
    vk::RenderingAttachmentInfo colour_attachment{};
    colour_attachment.imageView = view;
    colour_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    colour_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    colour_attachment.storeOp = vk::AttachmentStoreOp::eStore;
    colour_attachment.clearValue.color = vk::ClearColorValue{std::array{0.0f, 0.1f, 0.15f, 1.0f}};

    vk::RenderingInfo rendering_info{};
    rendering_info.renderArea.offset = vk::Offset2D{0, 0};
    rendering_info.renderArea.extent = extent;
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &colour_attachment;

    cmd.beginRendering(rendering_info);

    // Bind pipeline and set dynamic state
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

    // Bind vertex buffer and draw
    vk::DeviceSize offset = 0;
    cmd.bindVertexBuffers(0, {vertex_buffer}, {offset});
    cmd.draw(3, 1, 0, 0);

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
    sends RenderEvent messages via Signal<T>. Owns the Vulkan rendering timeline —
    command recording, submission, and presentation all happen here.
*/
static void renderThread(Device& device, Swapchain& swapchain, Pipeline& pipeline, VkBuffer vertex_buffer, vk::raii::CommandPool& command_pool,
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

    uint32_t current_frame{0};

    while (true) {
        // Block until the main thread sends an event
        {
            std::unique_lock<std::mutex> lock(render_mutex);
            render_cv.wait(lock, [&render_signal]() {
                return !render_signal.empty();
            });
        }

        // Drain all pending events — coalesce multiple resizes into the last one
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
            case RenderEvent::Type::Stop:
                should_stop = true;
                break;
            }
        }

        if (should_stop) {
            device.get().waitIdle();
            return;
        }

        // Handle swapchain recreation — skip if minimised (0x0)
        if (needs_resize) {
            if (resize_width > 0 && resize_height > 0) {
                swapchain.recreate(resize_width, resize_height);
                rebuildPresentSemaphores();
            } else {
                // Minimised — do not render
                should_render = false;
            }
        }

        // Skip rendering while minimised or if no render was requested
        if (!should_render || swapchain.extent().width == 0 || swapchain.extent().height == 0) {
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
            }
            continue;
        }

        bool swapchain_suboptimal = (acquire_result == vk::Result::eSuboptimalKHR);

        // Only reset fence after we know we will submit work
        device.get().resetFences({*in_flight_fences[current_frame]});

        // Record command buffer
        const vk::raii::CommandBuffer& cmd = command_buffers[current_frame];
        cmd.reset();
        recordFrame(cmd, swapchain.images()[image_index], *swapchain.views()[image_index], swapchain.extent(), pipeline, vertex_buffer);

        // Submit
        vk::SemaphoreSubmitInfo wait_sem{};
        wait_sem.semaphore = *image_available_semaphores[current_frame];
        wait_sem.stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;

        vk::SemaphoreSubmitInfo signal_sem{};
        signal_sem.semaphore = *render_finished_semaphores[image_index];
        signal_sem.stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;

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
            }
            continue;
        }

        if (present_result == vk::Result::eSuboptimalKHR || swapchain_suboptimal) {
            if (swapchain.extent().width > 0 && swapchain.extent().height > 0) {
                swapchain.recreate(swapchain.extent().width, swapchain.extent().height);
                rebuildPresentSemaphores();
            }
        }

        current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
    }
}

int main()
{
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

        // Load shaders and create pipeline
        std::string exe_dir = executableDirectory();
        std::vector<uint32_t> spirv = loadSpirv(exe_dir + "triangle.spv", logger);

        // Create swapchain
        Swapchain swapchain(device, *surface, config.width, config.height, logger);

        // Graphics pipeline
        Pipeline pipeline(device, swapchain.format().format, spirv, spirv, logger);

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

        // Triangle vertex buffer — staging upload to GPU-local memory
        constexpr vk::DeviceSize VERTEX_BUFFER_SIZE = sizeof(TRIANGLE_VERTICES);

        AllocatedBuffer vertex_buffer = allocator.createBuffer(VERTEX_BUFFER_SIZE,
            static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst), 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

        // Staging buffer scoped so it is freed immediately after the copy completes
        {
            AllocatedBuffer staging_buffer = allocator.createBuffer(VERTEX_BUFFER_SIZE, static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc),
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_AUTO);

            VmaAllocationInfo staging_info = staging_buffer.allocationInfo();
            std::memcpy(staging_info.pMappedData, TRIANGLE_VERTICES.data(), VERTEX_BUFFER_SIZE);

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
            region.size = VERTEX_BUFFER_SIZE;
            copy_cmd.copyBuffer(staging_buffer.buffer(), vertex_buffer.buffer(), {region});

            copy_cmd.end();

            vk::SubmitInfo copy_submit{};
            copy_submit.commandBufferCount = 1;
            vk::CommandBuffer raw_cmd = *copy_cmd;
            copy_submit.pCommandBuffers = &raw_cmd;
            device.graphicsQueue().submit({copy_submit});
            device.graphicsQueue().waitIdle();
        }

        logger.logInfo("Triangle vertex buffer uploaded to GPU.");

        // Render event signal — main thread emits, render thread consumes
        SignalsLib::Signal<RenderEvent> render_signal;
        std::mutex render_mutex;
        std::condition_variable render_cv;

        // Spawn the render thread
        std::thread render_worker(renderThread, std::ref(device), std::ref(swapchain), std::ref(pipeline), vertex_buffer.buffer(), std::ref(command_pool),
            std::ref(command_buffers), std::ref(render_signal), std::ref(render_mutex), std::ref(render_cv), std::ref(logger));

        //! Emits a render event and wakes the render thread.
        auto emitRenderEvent = [&](RenderEvent event) {
            render_signal.emit(event);
            render_cv.notify_one();
        };

        // Immediate event callback — fires from wndProc/handleEvent, including during
        // Win32 modal resize loops when the main event loop is blocked. This ensures the
        // render thread receives resize/expose events without delay.
        struct CallbackContext {
            SignalsLib::Signal<RenderEvent>* signal;
            std::condition_variable* cv;
        };

        CallbackContext cb_ctx{&render_signal, &render_cv};

        window->setEventCallback(
            [](const WindowLib::WindowEvent& ev, void* user_data) {
                CallbackContext* ctx = static_cast<CallbackContext*>(user_data);
                switch (ev.type) {
                case WindowLib::WindowEvent::Type::Resize:
                    ctx->signal->emit({RenderEvent::Type::Resize, ev.resize.width, ev.resize.height});
                    ctx->cv->notify_one();
                    break;
                case WindowLib::WindowEvent::Type::Expose:
                    ctx->signal->emit({RenderEvent::Type::Render, 0, 0});
                    ctx->cv->notify_one();
                    break;
                default:
                    break;
                }
            },
            &cb_ctx);

        // Request initial frame render
        emitRenderEvent({RenderEvent::Type::Render, 0, 0});

        logger.logInfo("Press ESC to close.");

        // Main loop — handles non-rendering events (Close, KeyDown).
        // Resize and Expose are forwarded to the render thread via the immediate callback above.
#ifdef _WIN32
        constexpr uint32_t ESC_KEYCODE = 27;
#else
        constexpr uint32_t ESC_KEYCODE = 9;
#endif

        while (!window->shouldClose()) {
            window->waitEvents();

            WindowLib::WindowEvent ev;
            while (window->pollEvent(ev)) {
                switch (ev.type) {
                case WindowLib::WindowEvent::Type::Close:
                    logger.logInfo("Close requested.");
                    break;

                case WindowLib::WindowEvent::Type::KeyDown:
                    if (ev.key.keycode == ESC_KEYCODE) {
                        window->requestClose();
                    }
                    break;

                default:
                    break;
                }
            }
        }

        // Signal render thread to stop and wait for it
        emitRenderEvent({RenderEvent::Type::Stop, 0, 0});
        render_worker.join();

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
