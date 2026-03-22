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

#pragma once

#include "window_event.hpp"
#include <log/logger.hpp>
#include <memory>
#include <queue>
#include <string>
#include <cstdint>

namespace WindowLib
{

    //! Configuration parameters for creating a platform window.
    struct WindowConfig {
        std::string title{"TRON Grid Renderer"}; //!< Window title text.
        uint32_t width{1920}; //!< Initial window width in pixels.
        uint32_t height{1080}; //!< Initial window height in pixels.
        bool resizable{true}; //!< Whether the window can be resized by the user.
        bool decorated{true}; //!< Window chrome (title bar, borders).
    };

    //! Abstract base class for platform-native windows.
    class Window {
    public:
        virtual ~Window() = default;

        //! Non-copyable, non-movable (platform handles).
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;

        //! Processes pending platform events into the event queue (non-blocking).
        virtual void pumpEvents() = 0;

        //! Blocks until at least one platform event arrives, then drains all pending events.
        virtual void waitEvents() = 0;

        //! Captures or releases the mouse cursor for FPS-style look.
        //! When captured: cursor is hidden, locked to the window, and deltas are computed from centre.
        //! Must be called from the main (event) thread.
        virtual void setCursorCaptured(bool captured) = 0;

        //! Returns true if the cursor is currently captured.
        [[nodiscard]] bool cursorCaptured() const
        {
            return m_cursor_captured;
        }

        //! Returns the platform-native window handle (HWND on Win32, xcb_window_t via void* on XCB).
        [[nodiscard]] virtual void* nativeHandle() const = 0;

        //! Returns the platform-native display/instance (HINSTANCE on Win32, xcb_connection_t* on XCB).
        [[nodiscard]] virtual void* nativeDisplay() const = 0;

        //! Polls the next event from the queue (returns false if empty).
        [[nodiscard]] bool pollEvent(WindowEvent& out)
        {
            if (m_event_queue.empty()) {
                return false;
            }
            out = m_event_queue.front();
            m_event_queue.pop();
            return true;
        }

        //! Returns current window client-area width in pixels.
        [[nodiscard]] uint32_t width() const
        {
            return m_width;
        }

        //! Returns current window client-area height in pixels.
        [[nodiscard]] uint32_t height() const
        {
            return m_height;
        }

        //! Returns true if the window has been asked to close.
        [[nodiscard]] bool shouldClose() const
        {
            return m_should_close;
        }

        //! Requests the window to close (sets the close flag).
        void requestClose()
        {
            m_should_close = true;
        }

        //! Sets a callback that fires immediately when an event is pushed (from the platform thread).
        //! This is called in addition to the internal event queue, allowing the caller to react
        //! to events during modal operations (e.g., Win32 resize drag) when the main loop is blocked.
        using EventCallback = void (*)(const WindowEvent& ev, void* user_data);
        void setEventCallback(EventCallback callback, void* user_data)
        {
            m_event_callback = callback;
            m_event_callback_data = user_data;
        }

    protected:
        //! Constructs the base window with a logger reference.
        explicit Window(LoggingLib::Logger& logger) :
            m_logger(logger)
        {
        }

        //! Pushes an event onto the internal event queue and invokes the immediate callback.
        void pushEvent(const WindowEvent& ev)
        {
            m_event_queue.push(ev);
            if (m_event_callback) {
                m_event_callback(ev, m_event_callback_data);
            }
        }

        LoggingLib::Logger& m_logger; //!< Logger reference (non-owning).
        uint32_t m_width{0}; //!< Current client-area width in pixels.
        uint32_t m_height{0}; //!< Current client-area height in pixels.
        bool m_should_close{false}; //!< True after a close has been requested.
        bool m_cursor_captured{false}; //!< True when mouse cursor is captured for FPS look.

    private:
        std::queue<WindowEvent> m_event_queue; //!< Pending window events waiting to be polled.
        EventCallback m_event_callback{nullptr}; //!< Immediate event callback (may be null).
        void* m_event_callback_data{nullptr}; //!< User data for the event callback.
    };

    //! Factory — creates the platform-appropriate window (Win32 or XCB).
    //! Consumers never need to include platform headers.
    [[nodiscard]] std::unique_ptr<Window> create(const WindowConfig& config, LoggingLib::Logger& logger);

} // namespace WindowLib
