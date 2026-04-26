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
#include <string>

//! Looks up or creates an X11 atom by name.
[[nodiscard]] static xcb_atom_t internAtom(xcb_connection_t* conn, const char* name)
{
    xcb_intern_atom_cookie_t cookie{xcb_intern_atom(conn, 0, strlen(name), name)};
    xcb_intern_atom_reply_t* reply{xcb_intern_atom_reply(conn, cookie, nullptr)};
    if (!reply) {
        return XCB_ATOM_NONE;
    }
    xcb_atom_t atom{reply->atom};
    free(reply);
    return atom;
}

namespace WindowLib
{

    XcbWindow::XcbWindow(const WindowConfig& config, LoggingLib::Logger& logger) :
        Window(logger)
    {
        // Connect to X server
        int screen_num{0};
        m_connection = xcb_connect(nullptr, &screen_num);

        if (xcb_connection_has_error(m_connection)) {
            m_logger.logFatal("Failed to connect to X server.");
            std::abort();
            return;
        }

        // Get the screen. Guard against pathological xcb_connect responses where
        // screen_num >= the number of screens reported by the setup — without the
        // guard, advancing past the end leaves iter.data == nullptr and the next
        // m_screen->width_in_pixels access segfaults.
        const xcb_setup_t* setup{xcb_get_setup(m_connection)};
        xcb_screen_iterator_t iter{xcb_setup_roots_iterator(setup)};
        for (int i{0}; (i < screen_num) && (iter.rem > 0); ++i) {
            xcb_screen_next(&iter);
        }
        if ((iter.rem == 0) || (iter.data == nullptr)) {
            m_logger.logFatal("X server reported no usable screen (screen_num=" + std::to_string(screen_num) + ").");
            std::abort();
            return;
        }
        m_screen = iter.data;

        // Create window
        m_window = xcb_generate_id(m_connection);

        uint32_t event_mask{XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE
            | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_FOCUS_CHANGE};

        // Do not set XCB_CW_BACK_PIXEL — omitting the background pixel prevents the
        // X server from painting a background colour during resize, so the old Vulkan
        // frame stays visible until the render thread presents the new one.
        uint32_t value_mask{XCB_CW_EVENT_MASK};
        uint32_t value_list[] = {event_mask};

        // Centre on screen
        int16_t x{static_cast<int16_t>((static_cast<int32_t>(m_screen->width_in_pixels) - static_cast<int32_t>(config.width)) / 2)};
        int16_t y{static_cast<int16_t>((static_cast<int32_t>(m_screen->height_in_pixels) - static_cast<int32_t>(config.height)) / 2)};

        xcb_create_window(m_connection, XCB_COPY_FROM_PARENT, m_window, m_screen->root, x, y, config.width, config.height,
            0, // border width
            XCB_WINDOW_CLASS_INPUT_OUTPUT, m_screen->root_visual, value_mask, value_list);

        m_width = config.width;
        m_height = config.height;

        // Set window title twice — once with the legacy ICCCM WM_NAME (Latin-1, picked up
        // by older window managers) and once with the modern EWMH _NET_WM_NAME (UTF-8,
        // preferred by GNOME / KDE / every contemporary WM). Without the second one,
        // multibyte UTF-8 sequences in config.title display as garbage on non-ASCII
        // characters.
        xcb_change_property(m_connection, XCB_PROP_MODE_REPLACE, m_window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, static_cast<uint32_t>(config.title.length()),
            config.title.c_str());

        xcb_atom_t net_wm_name{internAtom(m_connection, "_NET_WM_NAME")};
        xcb_atom_t utf8_string{internAtom(m_connection, "UTF8_STRING")};
        if ((net_wm_name != XCB_ATOM_NONE) && (utf8_string != XCB_ATOM_NONE)) {
            xcb_change_property(m_connection, XCB_PROP_MODE_REPLACE, m_window, net_wm_name, utf8_string, 8, static_cast<uint32_t>(config.title.length()),
                config.title.c_str());
        }

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

            xcb_atom_t wm_normal_hints{internAtom(m_connection, "WM_NORMAL_HINTS")};
            xcb_atom_t wm_size_hints{internAtom(m_connection, "WM_SIZE_HINTS")};
            xcb_change_property(m_connection, XCB_PROP_MODE_REPLACE, m_window, wm_normal_hints, wm_size_hints, 32, sizeof(hints) / 4, &hints);
        }

        // Map (show) the window
        xcb_map_window(m_connection, m_window);
        xcb_flush(m_connection);
    }

    XcbWindow::~XcbWindow()
    {
        // Defensive cursor free — if the program is shutting down while still in capture
        // mode, release the cursor before tearing down the connection so the X server
        // doesn't end up holding an orphaned reference. Releasing the pointer grab
        // first lets the cursor handle drop cleanly.
        if (m_invisible_cursor != 0) {
            if (m_cursor_captured) {
                xcb_ungrab_pointer(m_connection, XCB_CURRENT_TIME);
            }
            xcb_free_cursor(m_connection, m_invisible_cursor);
            m_invisible_cursor = 0;
        }

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
        xcb_generic_event_t* event{xcb_wait_for_event(m_connection)};
        if (event) {
            handleEvent(event);
            free(event);
        }

        // Drain any remaining pending events
        pumpEvents();
    }

    void XcbWindow::setCursorCaptured(bool captured)
    {
        // Idempotent guard — repeated calls with the same value are no-ops. Without this,
        // double-toggle would leak cursor handles and double-grab the pointer.
        if (m_cursor_captured == captured) {
            return;
        }
        m_cursor_captured = captured;
        if (captured) {
            // Create an invisible cursor. The previous code created a 1×1 depth-1 pixmap
            // without initialising it; per the X11 spec, freshly-created pixmap contents
            // are undefined, so on some servers the "invisible" cursor would render as a
            // visible single pixel. Explicitly zero the pixmap with a GC fill so the
            // cursor is guaranteed transparent.
            xcb_pixmap_t pixmap{xcb_generate_id(m_connection)};
            xcb_create_pixmap(m_connection, 1, pixmap, m_window, 1, 1);

            xcb_gcontext_t gc{xcb_generate_id(m_connection)};
            uint32_t fg{0};
            xcb_create_gc(m_connection, gc, pixmap, XCB_GC_FOREGROUND, &fg);

            xcb_rectangle_t rect{0, 0, 1, 1};
            xcb_poly_fill_rectangle(m_connection, pixmap, gc, 1, &rect);

            xcb_free_gc(m_connection, gc);

            // Build the cursor from the (now zeroed) pixmap. xcb_create_cursor takes a
            // source bitmap and a mask bitmap; the mask determines which pixels are drawn
            // at all (mask bit 0 = transparent, mask bit 1 = use source colour). With the
            // mask all-zero, every pixel is transparent regardless of source — so passing
            // the same zeroed pixmap for both arguments yields a fully invisible cursor.
            m_invisible_cursor = xcb_generate_id(m_connection);
            xcb_create_cursor(m_connection, m_invisible_cursor, pixmap, pixmap, 0, 0, 0, 0, 0, 0, 0, 0);

            // Pixmap can be freed now — xcb_create_cursor copies the bitmap data into the
            // server-side cursor object.
            xcb_free_pixmap(m_connection, pixmap);

            // Grab pointer with the invisible cursor.
            xcb_grab_pointer_cookie_t grab_cookie{
                xcb_grab_pointer(m_connection, 1, m_window, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION,
                    XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, m_window, m_invisible_cursor, XCB_CURRENT_TIME)};
            xcb_grab_pointer_reply_t* grab_reply{xcb_grab_pointer_reply(m_connection, grab_cookie, nullptr)};
            if (grab_reply != nullptr) {
                bool grab_ok{grab_reply->status == XCB_GRAB_STATUS_SUCCESS};
                free(grab_reply);
                if (!grab_ok) {
                    m_cursor_captured = false;
                    // Grab failed — release the cursor we just created.
                    xcb_free_cursor(m_connection, m_invisible_cursor);
                    m_invisible_cursor = 0;
                    xcb_flush(m_connection);
                    return;
                }
            }

            // Centre cursor. The warp triggers a synthetic XCB_MOTION_NOTIFY — flag it so
            // the motion handler consumes the event without emitting a duplicate.
            xcb_warp_pointer(m_connection, XCB_NONE, m_window, 0, 0, 0, 0, static_cast<int16_t>(m_width / 2), static_cast<int16_t>(m_height / 2));
            m_last_mouse_x = static_cast<int32_t>(m_width / 2);
            m_last_mouse_y = static_cast<int32_t>(m_height / 2);
            m_warp_pending = true;

            // The cursor is intentionally NOT freed here — the pointer grab still
            // references it, and the X server's behaviour when a still-referenced cursor
            // is freed is implementation-defined. The cursor lives on m_invisible_cursor
            // for the lifetime of the grab and is freed when capture is released below.

            xcb_flush(m_connection);
        } else {
            xcb_ungrab_pointer(m_connection, XCB_CURRENT_TIME);

            // Now that the grab is released, it's safe to free the cursor handle.
            if (m_invisible_cursor != 0) {
                xcb_free_cursor(m_connection, m_invisible_cursor);
                m_invisible_cursor = 0;
            }

            xcb_flush(m_connection);
        }
    }

    void XcbWindow::handleEvent(xcb_generic_event_t* event)
    {
        uint8_t event_type{static_cast<uint8_t>(event->response_type & 0x7F)};

        switch (event_type) {
        case XCB_CLIENT_MESSAGE: {
            auto* cm{reinterpret_cast<xcb_client_message_event_t*>(event)};
            if (cm->data.data32[0] == m_wm_delete_window) {
                WindowEvent ev{WindowEvent::Type::Close};
                pushEvent(ev);
                m_should_close = true;
            }
            break;
        }

        case XCB_EXPOSE: {
            WindowEvent ev{WindowEvent::Type::Expose};
            pushEvent(ev);
            break;
        }

        case XCB_CONFIGURE_NOTIFY: {
            auto* cfg{reinterpret_cast<xcb_configure_notify_event_t*>(event)};
            if (cfg->width != m_width || cfg->height != m_height) {
                m_width = cfg->width;
                m_height = cfg->height;
                WindowEvent ev{WindowEvent::Type::Resize};
                ev.resize.width = cfg->width;
                ev.resize.height = cfg->height;
                pushEvent(ev);
            }
            break;
        }

        case XCB_KEY_PRESS: {
            auto* kp{reinterpret_cast<xcb_key_press_event_t*>(event)};
            WindowEvent ev{WindowEvent::Type::KeyDown};
            ev.key.keycode = kp->detail; // X11 keycode (not keysym)
            ev.key.repeat = false; // X11 doesn't distinguish easily
            pushEvent(ev);
            break;
        }

        case XCB_KEY_RELEASE: {
            auto* kr{reinterpret_cast<xcb_key_release_event_t*>(event)};
            WindowEvent ev{WindowEvent::Type::KeyUp};
            ev.key.keycode = kr->detail;
            ev.key.repeat = false;
            pushEvent(ev);
            break;
        }

        case XCB_MOTION_NOTIFY: {
            xcb_motion_notify_event_t* mn{reinterpret_cast<xcb_motion_notify_event_t*>(event)};
            int32_t x{static_cast<int32_t>(mn->event_x)};
            int32_t y{static_cast<int32_t>(mn->event_y)};

            // Filter the synthetic recentre event. After xcb_warp_pointer (in this branch
            // or in setCursorCaptured), the X server dispatches a follow-up MOTION_NOTIFY
            // with the new position. Without this filter, every real mouse move emits two
            // events — one real, one phantom (dx=0, dy=0) — doubling consumer event load
            // and potentially confusing camera-look code that integrates dx/dy.
            if (m_warp_pending) {
                m_warp_pending = false;
                m_last_mouse_x = x;
                m_last_mouse_y = y;
                m_mouse_tracked = true;
                break;
            }

            int32_t dx{m_mouse_tracked ? (x - m_last_mouse_x) : 0};
            int32_t dy{m_mouse_tracked ? (y - m_last_mouse_y) : 0};

            if (m_cursor_captured && (dx != 0 || dy != 0)) {
                // Recentre cursor to prevent hitting screen edges
                int16_t cx{static_cast<int16_t>(m_width / 2)};
                int16_t cy{static_cast<int16_t>(m_height / 2)};
                m_last_mouse_x = static_cast<int32_t>(cx);
                m_last_mouse_y = static_cast<int32_t>(cy);
                xcb_warp_pointer(m_connection, XCB_NONE, m_window, 0, 0, 0, 0, cx, cy);
                xcb_flush(m_connection);
                m_warp_pending = true; // The next XCB_MOTION_NOTIFY will be the synthetic recentre.
            } else {
                m_last_mouse_x = x;
                m_last_mouse_y = y;
            }

            WindowEvent ev{WindowEvent::Type::MouseMove};
            ev.mouse_move.x = x;
            ev.mouse_move.y = y;
            ev.mouse_move.dx = dx;
            ev.mouse_move.dy = dy;
            pushEvent(ev);

            m_mouse_tracked = true;
            break;
        }

        case XCB_BUTTON_PRESS: {
            auto* bp{reinterpret_cast<xcb_button_press_event_t*>(event)};
            // X11: 1=left, 2=middle, 3=right, 4/5=scroll
            if (bp->detail >= 1 && bp->detail <= 3) {
                WindowEvent ev{WindowEvent::Type::MouseButtonDown};
                ev.mouse_button.button = (bp->detail == 1) ? 0 : (bp->detail == 3) ? 1 : 2;
                ev.mouse_button.x = bp->event_x;
                ev.mouse_button.y = bp->event_y;
                pushEvent(ev);
            }
            break;
        }

        case XCB_BUTTON_RELEASE: {
            auto* br{reinterpret_cast<xcb_button_release_event_t*>(event)};
            if (br->detail >= 1 && br->detail <= 3) {
                WindowEvent ev{WindowEvent::Type::MouseButtonUp};
                ev.mouse_button.button = (br->detail == 1) ? 0 : (br->detail == 3) ? 1 : 2;
                ev.mouse_button.x = br->event_x;
                ev.mouse_button.y = br->event_y;
                pushEvent(ev);
            }
            break;
        }

        case XCB_FOCUS_IN: {
            WindowEvent ev{WindowEvent::Type::Focus};
            pushEvent(ev);
            break;
        }

        case XCB_FOCUS_OUT: {
            WindowEvent ev{WindowEvent::Type::Blur};
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
