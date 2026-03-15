/*
 * TronGrid — entry point
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#include "gpu_device.hpp"
#include "gpu_instance.hpp"
#include "gpu_surface.hpp"
#include "gpu_swapchain.hpp"

#include <signal/signal.hpp>
#include <window/window.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

//! resize event carried through Signal<T>.
struct ResizeEvent {
    uint32_t width = 0;
    uint32_t height = 0;
};

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

static void recordClearCommand(const vk::raii::CommandBuffer& cmd, vk::Image image, vk::ImageView view, vk::Extent2D extent)
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
    to_colour.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_colour.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
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
    cmd.endRendering();

    // Transition: COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC
    vk::ImageMemoryBarrier2 to_present{};
    to_present.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    to_present.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
    to_present.dstStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe;
    to_present.dstAccessMask = vk::AccessFlagBits2::eNone;
    to_present.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
    to_present.newLayout = vk::ImageLayout::ePresentSrcKHR;
    to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
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

int main()
{
    try {
        // Create window
        WindowConfig config{
            .title = "TRON Grid Renderer",
            .width = 1280,
            .height = 720,
        };

        std::unique_ptr<Window> window = WindowLib::create(config);
        std::cout << "Window created: " << config.width << "x" << config.height << "\n";

        // Vulkan initialisation
        constexpr bool ENABLE_VALIDATION =
#ifdef NDEBUG
            false;
#else
            true;
#endif

        Gpu::Instance instance(ENABLE_VALIDATION, Gpu::requiredSurfaceExtensions());
        vk::raii::SurfaceKHR surface = Gpu::createSurface(instance.get(), *window);
        Gpu::Device device(instance, *surface);

        std::cout << "Vulkan ready - GPU: " << device.name() << "\n";

        // Create swapchain
        Gpu::Swapchain swapchain(device, *surface, config.width, config.height);

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

        // Frame synchronisation
        std::vector<vk::raii::Semaphore> image_available_semaphores;
        std::vector<vk::raii::Semaphore> render_finished_semaphores;
        std::vector<vk::raii::Fence> in_flight_fences;

        vk::SemaphoreCreateInfo sem_info{};
        vk::FenceCreateInfo fence_info{};
        fence_info.flags = vk::FenceCreateFlagBits::eSignaled; // start signalled so first wait succeeds

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            image_available_semaphores.push_back(vk::raii::Semaphore(device.get(), sem_info));
            render_finished_semaphores.push_back(vk::raii::Semaphore(device.get(), sem_info));
            in_flight_fences.push_back(vk::raii::Fence(device.get(), fence_info));
        }

        uint32_t current_frame = 0;

        // Resize signal — event loop emits, render loop consumes
        SignalLib::Signal<ResizeEvent> resize_signal;

        std::cout << "Press ESC to close\n";

        // Main loop
        while (!window->shouldClose()) {
            window->pumpEvents();

            WindowEvent ev;
#ifdef _WIN32
            constexpr uint32_t ESC_KEYCODE = 27; // Win32 virtual key code
#else
            constexpr uint32_t ESC_KEYCODE = 9; // X11 keycode
#endif
            while (window->pollEvent(ev)) {
                switch (ev.type) {
                case WindowEvent::Type::Close:
                    std::cout << "Close requested\n";
                    break;

                case WindowEvent::Type::Resize:
                    resize_signal.emit({ev.resize.width, ev.resize.height});
                    break;

                case WindowEvent::Type::KeyDown:
                    if (ev.key.keycode == ESC_KEYCODE) {
                        window->requestClose();
                    }
                    break;

                default:
                    break;
                }
            }

            // Consume pending resize events — recreate swapchain with latest dimensions
            {
                ResizeEvent resize_ev;
                bool needs_recreate = false;
                while (resize_signal.consume(resize_ev)) {
                    needs_recreate = true;
                }
                if (needs_recreate && resize_ev.width > 0 && resize_ev.height > 0) {
                    swapchain.recreate(resize_ev.width, resize_ev.height);
                }
            }

            // Skip rendering while minimised
            if (window->width() == 0 || window->height() == 0) {
                continue;
            }

            // Wait for this frame's fence
            vk::Result wait_result = device.get().waitForFences({*in_flight_fences[current_frame]}, VK_TRUE, std::numeric_limits<uint64_t>::max());
            if (wait_result != vk::Result::eSuccess) {
                std::cerr << "[TronGrid] Fatal: failed to wait for fence\n";
                std::abort();
                return 1;
            }

            // Acquire next swapchain image
            uint32_t image_index = 0;
            vk::Result acquire_result = vk::Result::eSuccess;
            try {
                vk::ResultValue<uint32_t> acquire = swapchain.get().acquireNextImage(std::numeric_limits<uint64_t>::max(), *image_available_semaphores[current_frame]);
                acquire_result = acquire.result;
                image_index = acquire.value;
            } catch (const vk::OutOfDateKHRError&) {
                if (window->width() > 0 && window->height() > 0) {
                    swapchain.recreate(window->width(), window->height());
                }
                continue;
            }

            bool swapchain_suboptimal = (acquire_result == vk::Result::eSuboptimalKHR);

            // Only reset fence after we know we will submit work
            device.get().resetFences({*in_flight_fences[current_frame]});

            // Record command buffer
            const vk::raii::CommandBuffer& cmd = command_buffers[current_frame];
            cmd.reset();
            recordClearCommand(cmd, swapchain.images()[image_index], *swapchain.views()[image_index], swapchain.extent());

            // Submit
            vk::SemaphoreSubmitInfo wait_sem{};
            wait_sem.semaphore = *image_available_semaphores[current_frame];
            wait_sem.stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;

            vk::SemaphoreSubmitInfo signal_sem{};
            signal_sem.semaphore = *render_finished_semaphores[current_frame];
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
            present_info.pWaitSemaphores = &*render_finished_semaphores[current_frame];
            present_info.swapchainCount = 1;
            present_info.pSwapchains = swapchains;
            present_info.pImageIndices = &image_index;

            vk::Result present_result = vk::Result::eSuccess;
            try {
                present_result = device.presentQueue().presentKHR(present_info);
            } catch (const vk::OutOfDateKHRError&) {
                if (window->width() > 0 && window->height() > 0) {
                    swapchain.recreate(window->width(), window->height());
                }
                continue;
            }

            if (present_result == vk::Result::eSuboptimalKHR || swapchain_suboptimal) {
                if (window->width() > 0 && window->height() > 0) {
                    swapchain.recreate(window->width(), window->height());
                }
            }

            current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
        }

        // Wait for GPU to finish before destroying resources
        device.get().waitIdle();

        std::cout << "Shutting down\n";

    } catch (const vk::SystemError& e) {
        std::cerr << "Vulkan error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
