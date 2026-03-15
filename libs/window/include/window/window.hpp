/*
    TronGrid — abstract window interface
    Copyright (C) 2026 Matej Gomboc
    SPDX-Licence-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "window_event.hpp"
#include <memory>
#include <queue>
#include <string>
#include <cstdint>

namespace WindowLib
{

struct WindowConfig {
    std::string title = "TRON Grid Renderer";
    uint32_t width = 1920;
    uint32_t height = 1080;
    bool resizable = true;
    bool decorated = true; //!< window chrome (title bar, borders)
};

class Window {
public:
    virtual ~Window() = default;

    //! Non-copyable, non-movable (platform handles)
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    //! Process pending platform events into the event queue
    virtual void pumpEvents() = 0;

    //! Platform-native window handle (HWND on Win32, xcb_window_t via void* on XCB)
    virtual void* nativeHandle() const = 0;

    //! Platform-native display/instance (HINSTANCE on Win32, xcb_connection_t* on XCB)
    virtual void* nativeDisplay() const = 0;

    //! Poll next event from queue (returns false if empty)
    [[nodiscard]] bool pollEvent(WindowEvent& out)
    {
        if (m_event_queue.empty()) {
            return false;
        }
        out = m_event_queue.front();
        m_event_queue.pop();
        return true;
    }

    // Accessors
    [[nodiscard]] uint32_t width() const
    {
        return m_width;
    }

    [[nodiscard]] uint32_t height() const
    {
        return m_height;
    }

    [[nodiscard]] bool shouldClose() const
    {
        return m_should_close;
    }

    void requestClose()
    {
        m_should_close = true;
    }

protected:
    Window() = default;

    void pushEvent(const WindowEvent& ev)
    {
        m_event_queue.push(ev);
    }

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_should_close = false;

private:
    std::queue<WindowEvent> m_event_queue;
};

//! Factory — creates the platform-appropriate window (Win32 or XCB).
//! Consumers never need to include platform headers.
[[nodiscard]] std::unique_ptr<Window> create(const WindowConfig& config);

} // namespace WindowLib
