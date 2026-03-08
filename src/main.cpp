#include "window.hpp"
#ifdef _WIN32
#include "win32_window.hpp"
#elif defined(__linux__)
#include "xcb_window.hpp"
#endif
#include <iostream>
#include <memory>

std::unique_ptr<TronGrid::Window> create_window(const TronGrid::WindowConfig& config)
{
#ifdef _WIN32
    return std::make_unique<TronGrid::Win32Window>(config);
#elif defined(__linux__)
    return std::make_unique<TronGrid::XcbWindow>(config);
#else
#error "Unsupported platform"
#endif
}

int main()
{
    try {
        TronGrid::WindowConfig config;
        config.title = "TRON Grid Renderer";
        config.width = 1280;
        config.height = 720;

        auto window = create_window(config);

        std::cout << "Window created: " << config.width << "x" << config.height << "\n";
        std::cout << "Press ESC to close\n";

        // Main loop
        while (!window->should_close()) {
            window->pump_events();

            TronGrid::WindowEvent ev;
            while (window->poll_event(ev)) {
                switch (ev.type) {
                case TronGrid::WindowEvent::Type::Close:
                    std::cout << "Close requested\n";
                    break;

                case TronGrid::WindowEvent::Type::Resize:
                    std::cout << "Resize: " << ev.resize.width << "x" << ev.resize.height << "\n";
                    break;

                case TronGrid::WindowEvent::Type::KeyDown:
                    std::cout << "Key down: " << ev.key.keycode;
                    if (ev.key.repeat)
                        std::cout << " (repeat)";
                    std::cout << "\n";

// ESC to close (Win32: 27, X11: 9)
#ifdef _WIN32
                    if (ev.key.keycode == 27)
                        window->request_close();
#else
                    if (ev.key.keycode == 9)
                        window->request_close();
#endif
                    break;

                case TronGrid::WindowEvent::Type::KeyUp:
                    std::cout << "Key up: " << ev.key.keycode << "\n";
                    break;

                case TronGrid::WindowEvent::Type::MouseMove:
                    // Uncomment for verbose mouse tracking:
                    // std::cout << "Mouse: " << ev.mouse_move.x << ", " << ev.mouse_move.y
                    //           << " (delta: " << ev.mouse_move.dx << ", " << ev.mouse_move.dy << ")\n";
                    break;

                case TronGrid::WindowEvent::Type::MouseButtonDown:
                    std::cout << "Mouse button down: " << (int)ev.mouse_button.button << " at (" << ev.mouse_button.x << ", " << ev.mouse_button.y << ")\n";
                    break;

                case TronGrid::WindowEvent::Type::MouseButtonUp:
                    std::cout << "Mouse button up: " << (int)ev.mouse_button.button << " at (" << ev.mouse_button.x << ", " << ev.mouse_button.y << ")\n";
                    break;

                case TronGrid::WindowEvent::Type::Focus:
                    std::cout << "Window focused\n";
                    break;

                case TronGrid::WindowEvent::Type::Blur:
                    std::cout << "Window lost focus\n";
                    break;

                default:
                    break;
                }
            }

// Here you would: render frame, present, etc.
// For now, just yield to avoid busy loop
#ifdef _WIN32
            Sleep(1);
#else
            usleep(1000);
#endif
        }

        std::cout << "Shutting down\n";

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
