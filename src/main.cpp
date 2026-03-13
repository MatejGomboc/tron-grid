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
        WindowConfig config;
        config.title = "TRON Grid Renderer";
        config.width = 1280;
        config.height = 720;

        std::unique_ptr<Window> window = window::create(config);
        std::cout << "Window created: " << config.width << "x" << config.height << "\n";

        // Vulkan initialisation
#ifdef NDEBUG
        bool enable_validation = false;
#else
        bool enable_validation = true;
#endif

        gpu::Instance instance(enable_validation, gpu::required_surface_extensions());

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
            while (window->poll_event(ev)) {
                switch (ev.type) {
                case WindowEvent::Type::Close:
                    std::cout << "Close requested\n";
                    break;

                case WindowEvent::Type::Resize:
                    std::cout << "Resize: " << ev.resize.width << "x" << ev.resize.height << "\n";
                    break;

                case WindowEvent::Type::KeyDown:
                    // ESC to close (Win32: 27, X11: 9)
#ifdef _WIN32
                    if (ev.key.keycode == 27) {
                        window->request_close();
                    }
#else
                    if (ev.key.keycode == 9) {
                        window->request_close();
                    }
#endif
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
