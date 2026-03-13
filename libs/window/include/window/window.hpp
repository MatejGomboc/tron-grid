/*
 * TronGrid — abstract window interface
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "window_event.hpp"
#include <memory>
#include <queue>
#include <string>
#include <cstdint>

// Forward declare Vulkan types to avoid including vulkan.h here
typedef struct VkInstance_T* VkInstance;
typedef struct VkSurfaceKHR_T* VkSurfaceKHR;

struct WindowConfig {
    std::string title = "TRON Grid Renderer";
    uint32_t width = 1920;
    uint32_t height = 1080;
    bool resizable = true;
    bool decorated = true; // window chrome (title bar, borders)
};

class Window {
public:
    virtual ~Window() = default;

    // Non-copyable, non-movable (platform handles)
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    // Process pending platform events into the event queue
    virtual void pump_events() = 0;

    // Create Vulkan surface for this window
    virtual VkSurfaceKHR create_surface(VkInstance instance) = 0;

    // Poll next event from queue (returns false if empty)
    bool poll_event(WindowEvent& out)
    {
        if (event_queue_.empty())
            return false;
        out = event_queue_.front();
        event_queue_.pop();
        return true;
    }

    // Accessors
    uint32_t width() const
    {
        return width_;
    }
    uint32_t height() const
    {
        return height_;
    }
    bool should_close() const
    {
        return should_close_;
    }

    void request_close()
    {
        should_close_ = true;
    }

protected:
    Window() = default;

    void push_event(const WindowEvent& ev)
    {
        event_queue_.push(ev);
    }

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool should_close_ = false;

private:
    std::queue<WindowEvent> event_queue_;
};

namespace window
{
    // Factory — creates the platform-appropriate window (Win32 or XCB).
    // Consumers never need to include platform headers.
    std::unique_ptr<Window> create(const WindowConfig& config);
} // namespace window
