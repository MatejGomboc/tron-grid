/*
 * TronGrid — entry point
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#include "gpu_device.hpp"
#include "gpu_instance.hpp"
#include "gpu_surface.hpp"

#include <window/window.hpp>

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

int main()
{
    try {
        // Create window
        WindowConfig config{
            .title = "TRON Grid Renderer",
            .width = 1280,
            .height = 720,
        };

        std::unique_ptr<Window> window = window::create(config);
        std::cout << "Window created: " << config.width << "x" << config.height << "\n";

        // Vulkan initialisation
        constexpr bool ENABLE_VALIDATION =
#ifdef NDEBUG
            false;
#else
            true;
#endif

        gpu::Instance instance(ENABLE_VALIDATION, gpu::required_surface_extensions());

        // Create Vulkan surface from the window's native handles
        vk::raii::SurfaceKHR surface = gpu::create_surface(instance.get(), *window);

        // Select GPU and create logical device
        gpu::Device device(instance, *surface);

        std::cout << "Vulkan ready - GPU: " << device.name() << "\n";
        std::cout << "Press ESC to close\n";

        // Main loop
        while (!window->should_close()) {
            window->pump_events();

            WindowEvent ev;
#ifdef _WIN32
            constexpr uint32_t ESC_KEYCODE = 27; // Win32 virtual key code
#else
            constexpr uint32_t ESC_KEYCODE = 9; // X11 keycode
#endif
            while (window->poll_event(ev)) {
                switch (ev.type) {
                case WindowEvent::Type::Close:
                    std::cout << "Close requested\n";
                    break;

                case WindowEvent::Type::Resize:
                    std::cout << "Resize: " << ev.resize.width << "x" << ev.resize.height << "\n";
                    break;

                case WindowEvent::Type::KeyDown:
                    if (ev.key.keycode == ESC_KEYCODE) {
                        window->request_close();
                    }
                    break;

                default:
                    break;
                }
            }

            // Here: render frame, present, etc.
            // For now, just yield to avoid busy loop
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
