#include <window/window.hpp>
#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    try {
        WindowConfig config;
        config.title = "TRON Grid Renderer";
        config.width = 1280;
        config.height = 720;

        auto window = window::create(config);

        std::cout << "Window created: " << config.width << "x" << config.height << "\n";
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

                case WindowEvent::Type::KeyUp:
                    std::cout << "Key up: " << ev.key.keycode << "\n";
                    break;

                case WindowEvent::Type::MouseMove:
                    // Uncomment for verbose mouse tracking:
                    // std::cout << "Mouse: " << ev.mouse_move.x << ", " << ev.mouse_move.y
                    //           << " (delta: " << ev.mouse_move.dx << ", " << ev.mouse_move.dy << ")\n";
                    break;

                case WindowEvent::Type::MouseButtonDown:
                    std::cout << "Mouse button down: " << (int)ev.mouse_button.button << " at (" << ev.mouse_button.x << ", " << ev.mouse_button.y << ")\n";
                    break;

                case WindowEvent::Type::MouseButtonUp:
                    std::cout << "Mouse button up: " << (int)ev.mouse_button.button << " at (" << ev.mouse_button.x << ", " << ev.mouse_button.y << ")\n";
                    break;

                case WindowEvent::Type::Focus:
                    std::cout << "Window focused\n";
                    break;

                case WindowEvent::Type::Blur:
                    std::cout << "Window lost focus\n";
                    break;

                default:
                    break;
                }
            }

            // Here you would: render frame, present, etc.
            // For now, just yield to avoid busy loop
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        std::cout << "Shutting down\n";

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
