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

#ifdef __linux__

#include "xcb_window.hpp"
#include <cstdlib>
#include <cstring>

//! Looks up or creates an X11 atom by name.
static xcb_atom_t internAtom(xcb_connection_t* conn, const char* name)
{
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, 0, strlen(name), name);
    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(conn, cookie, nullptr);
    if (!reply) {
        return XCB_ATOM_NONE;
    }
    xcb_atom_t atom = reply->atom;
    free(reply);
    return atom;
}

namespace WindowLib
{

    XcbWindow::XcbWindow(const WindowConfig& config, LoggingLib::Logger& logger) :
        Window(logger)
    {
        // Connect to X server
        int screen_num;
        m_connection = xcb_connect(nullptr, &screen_num);

        if (xcb_connection_has_error(m_connection)) {
            m_logger.logFatal("Failed to connect to X server.");
            std::abort();
            return;
        }

        // Get the screen
        const xcb_setup_t* setup = xcb_get_setup(m_connection);
        xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
        for (int i = 0; i < screen_num; ++i) {
            xcb_screen_next(&iter);
        }
        m_screen = iter.data;

        // Create window
        m_window = xcb_generate_id(m_connection);

        uint32_t event_mask = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE
            | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_FOCUS_CHANGE;

        // Do not set XCB_CW_BACK_PIXEL — omitting the background pixel prevents the
        // X server from painting a background colour during resize, so the old Vulkan
        // frame stays visible until the render thread presents the new one.
        uint32_t value_mask = XCB_CW_EVENT_MASK;
        uint32_t value_list[] = {event_mask};

        // Centre on screen
        int16_t x = static_cast<int16_t>((static_cast<int32_t>(m_screen->width_in_pixels) - static_cast<int32_t>(config.width)) / 2);
        int16_t y = static_cast<int16_t>((static_cast<int32_t>(m_screen->height_in_pixels) - static_cast<int32_t>(config.height)) / 2);

        xcb_create_window(m_connection, XCB_COPY_FROM_PARENT, m_window, m_screen->root, x, y, config.width, config.height,
            0, // border width
            XCB_WINDOW_CLASS_INPUT_OUTPUT, m_screen->root_visual, value_mask, value_list);

        m_width = config.width;
        m_height = config.height;

        // Set window title
        xcb_change_property(m_connection, XCB_PROP_MODE_REPLACE, m_window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, config.title.length(), config.title.c_str());

        // Set up WM_DELETE_WINDOW handling (so we get close events)
        m_wm_protocols = internAtom(m_connection, "WM_PROTOCOLS");
        m_wm_delete_window = internAtom(m_connection, "WM_DELETE_WINDOW");

        xcb_change_property(m_connection, XCB_PROP_MODE_REPLACE, m_window, m_wm_protocols, XCB_ATOM_ATOM, 32, 1, &m_wm_delete_window);

        // Handle resize hints if not resizable
        if (!config.resizable) {
            // WM_NORMAL_HINTS with min/max size = current size
            struct {
                uint32_t flags;
                int32_t x, y;
                int32_t width, height;
                int32_t min_width, min_height;
                int32_t max_width, max_height;
                int32_t width_inc, height_inc;
                int32_t min_aspect_num, min_aspect_den;
                int32_t max_aspect_num, max_aspect_den;
                int32_t base_width, base_height;
                uint32_t win_gravity;
            } hints = {};

            hints.flags = (1 << 4) | (1 << 5); // PMinSize | PMaxSize
            hints.min_width = hints.max_width = config.width;
            hints.min_height = hints.max_height = config.height;

            xcb_atom_t wm_normal_hints = internAtom(m_connection, "WM_NORMAL_HINTS");
            xcb_atom_t wm_size_hints = internAtom(m_connection, "WM_SIZE_HINTS");
            xcb_change_property(m_connection, XCB_PROP_MODE_REPLACE, m_window, wm_normal_hints, wm_size_hints, 32, sizeof(hints) / 4, &hints);
        }

        // Map (show) the window
        xcb_map_window(m_connection, m_window);
        xcb_flush(m_connection);
    }

    XcbWindow::~XcbWindow()
    {
        if (m_window) {
            xcb_destroy_window(m_connection, m_window);
        }
        if (m_connection) {
            xcb_disconnect(m_connection);
        }
    }

    void XcbWindow::pumpEvents()
    {
        xcb_generic_event_t* event;
        while ((event = xcb_poll_for_event(m_connection))) {
            handleEvent(event);
            free(event);
        }

        // Check for connection errors
        if (xcb_connection_has_error(m_connection)) {
            m_should_close = true;
        }
    }

    void XcbWindow::waitEvents()
    {
        // Block until at least one event arrives
        xcb_generic_event_t* event = xcb_wait_for_event(m_connection);
        if (event) {
            handleEvent(event);
            free(event);
        }

        // Drain any remaining pending events
        pumpEvents();
    }

    void XcbWindow::handleEvent(xcb_generic_event_t* event)
    {
        uint8_t event_type = event->response_type & 0x7F;

        switch (event_type) {
        case XCB_CLIENT_MESSAGE: {
            auto* cm = reinterpret_cast<xcb_client_message_event_t*>(event);
            if (cm->data.data32[0] == m_wm_delete_window) {
                WindowEvent ev(WindowEvent::Type::Close);
                pushEvent(ev);
                m_should_close = true;
            }
            break;
        }

        case XCB_EXPOSE: {
            WindowEvent ev(WindowEvent::Type::Expose);
            pushEvent(ev);
            break;
        }

        case XCB_CONFIGURE_NOTIFY: {
            auto* cfg = reinterpret_cast<xcb_configure_notify_event_t*>(event);
            if (cfg->width != m_width || cfg->height != m_height) {
                m_width = cfg->width;
                m_height = cfg->height;
                WindowEvent ev(WindowEvent::Type::Resize);
                ev.resize.width = cfg->width;
                ev.resize.height = cfg->height;
                pushEvent(ev);
            }
            break;
        }

        case XCB_KEY_PRESS: {
            auto* kp = reinterpret_cast<xcb_key_press_event_t*>(event);
            WindowEvent ev(WindowEvent::Type::KeyDown);
            ev.key.keycode = kp->detail; // X11 keycode (not keysym)
            ev.key.repeat = false; // X11 doesn't distinguish easily
            pushEvent(ev);
            break;
        }

        case XCB_KEY_RELEASE: {
            auto* kr = reinterpret_cast<xcb_key_release_event_t*>(event);
            WindowEvent ev(WindowEvent::Type::KeyUp);
            ev.key.keycode = kr->detail;
            ev.key.repeat = false;
            pushEvent(ev);
            break;
        }

        case XCB_MOTION_NOTIFY: {
            auto* mn = reinterpret_cast<xcb_motion_notify_event_t*>(event);
            WindowEvent ev(WindowEvent::Type::MouseMove);
            ev.mouse_move.x = mn->event_x;
            ev.mouse_move.y = mn->event_y;
            ev.mouse_move.dx = m_mouse_tracked ? (mn->event_x - m_last_mouse_x) : 0;
            ev.mouse_move.dy = m_mouse_tracked ? (mn->event_y - m_last_mouse_y) : 0;
            pushEvent(ev);

            m_last_mouse_x = mn->event_x;
            m_last_mouse_y = mn->event_y;
            m_mouse_tracked = true;
            break;
        }

        case XCB_BUTTON_PRESS: {
            auto* bp = reinterpret_cast<xcb_button_press_event_t*>(event);
            // X11: 1=left, 2=middle, 3=right, 4/5=scroll
            if (bp->detail >= 1 && bp->detail <= 3) {
                WindowEvent ev(WindowEvent::Type::MouseButtonDown);
                ev.mouse_button.button = (bp->detail == 1) ? 0 : (bp->detail == 3) ? 1 : 2;
                ev.mouse_button.x = bp->event_x;
                ev.mouse_button.y = bp->event_y;
                pushEvent(ev);
            }
            break;
        }

        case XCB_BUTTON_RELEASE: {
            auto* br = reinterpret_cast<xcb_button_release_event_t*>(event);
            if (br->detail >= 1 && br->detail <= 3) {
                WindowEvent ev(WindowEvent::Type::MouseButtonUp);
                ev.mouse_button.button = (br->detail == 1) ? 0 : (br->detail == 3) ? 1 : 2;
                ev.mouse_button.x = br->event_x;
                ev.mouse_button.y = br->event_y;
                pushEvent(ev);
            }
            break;
        }

        case XCB_FOCUS_IN: {
            WindowEvent ev(WindowEvent::Type::Focus);
            pushEvent(ev);
            break;
        }

        case XCB_FOCUS_OUT: {
            WindowEvent ev(WindowEvent::Type::Blur);
            pushEvent(ev);
            break;
        }
        }
    }

    void* XcbWindow::nativeHandle() const
    {
        // xcb_window_t is a uint32_t — cast to void* for platform-agnostic API
        return reinterpret_cast<void*>(static_cast<uintptr_t>(m_window));
    }

    void* XcbWindow::nativeDisplay() const
    {
        return static_cast<void*>(m_connection);
    }

} // namespace WindowLib

#endif // __linux__
