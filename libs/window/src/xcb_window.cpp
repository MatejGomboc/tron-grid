/*
 * TronGrid — XCB window implementation
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#ifdef __linux__

#include "window/xcb_window.hpp"
#include <cstring>
#include <stdexcept>

// Helper to intern an atom
static xcb_atom_t intern_atom(xcb_connection_t* conn, const char* name)
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

XcbWindow::XcbWindow(const WindowConfig& config)
{
    // Connect to X server
    int screen_num;
    connection_ = xcb_connect(nullptr, &screen_num);

    if (xcb_connection_has_error(connection_)) {
        throw std::runtime_error("Failed to connect to X server");
    }

    // Get the screen
    const xcb_setup_t* setup = xcb_get_setup(connection_);
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
    for (int i = 0; i < screen_num; ++i) {
        xcb_screen_next(&iter);
    }
    screen_ = iter.data;

    // Create window
    window_ = xcb_generate_id(connection_);

    uint32_t event_mask = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
        XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_FOCUS_CHANGE;

    uint32_t value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    uint32_t value_list[] = {screen_->black_pixel, event_mask};

    // Centre on screen
    int16_t x = (screen_->width_in_pixels - config.width) / 2;
    int16_t y = (screen_->height_in_pixels - config.height) / 2;

    xcb_create_window(connection_, XCB_COPY_FROM_PARENT, window_, screen_->root, x, y, config.width, config.height,
        0, // border width
        XCB_WINDOW_CLASS_INPUT_OUTPUT, screen_->root_visual, value_mask, value_list);

    width_ = config.width;
    height_ = config.height;

    // Set window title
    xcb_change_property(connection_, XCB_PROP_MODE_REPLACE, window_, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, config.title.length(), config.title.c_str());

    // Set up WM_DELETE_WINDOW handling (so we get close events)
    wm_protocols_ = intern_atom(connection_, "WM_PROTOCOLS");
    wm_delete_window_ = intern_atom(connection_, "WM_DELETE_WINDOW");

    xcb_change_property(connection_, XCB_PROP_MODE_REPLACE, window_, wm_protocols_, XCB_ATOM_ATOM, 32, 1, &wm_delete_window_);

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

        xcb_atom_t wm_normal_hints = intern_atom(connection_, "WM_NORMAL_HINTS");
        xcb_change_property(connection_, XCB_PROP_MODE_REPLACE, window_, wm_normal_hints, wm_normal_hints, 32, sizeof(hints) / 4, &hints);
    }

    // Map (show) the window
    xcb_map_window(connection_, window_);
    xcb_flush(connection_);
}

XcbWindow::~XcbWindow()
{
    if (window_) {
        xcb_destroy_window(connection_, window_);
    }
    if (connection_) {
        xcb_disconnect(connection_);
    }
}

void XcbWindow::pump_events()
{
    xcb_generic_event_t* event;
    while ((event = xcb_poll_for_event(connection_))) {
        handle_event(event);
        free(event);
    }

    // Check for connection errors
    if (xcb_connection_has_error(connection_)) {
        should_close_ = true;
    }
}

void XcbWindow::handle_event(xcb_generic_event_t* event)
{
    uint8_t event_type = event->response_type & 0x7F;

    switch (event_type) {
    case XCB_CLIENT_MESSAGE: {
        auto* cm = reinterpret_cast<xcb_client_message_event_t*>(event);
        if (cm->data.data32[0] == wm_delete_window_) {
            WindowEvent ev(WindowEvent::Type::Close);
            push_event(ev);
            should_close_ = true;
        }
        break;
    }

    case XCB_CONFIGURE_NOTIFY: {
        auto* cfg = reinterpret_cast<xcb_configure_notify_event_t*>(event);
        if (cfg->width != width_ || cfg->height != height_) {
            width_ = cfg->width;
            height_ = cfg->height;
            WindowEvent ev(WindowEvent::Type::Resize);
            ev.resize.width = cfg->width;
            ev.resize.height = cfg->height;
            push_event(ev);
        }
        break;
    }

    case XCB_KEY_PRESS: {
        auto* kp = reinterpret_cast<xcb_key_press_event_t*>(event);
        WindowEvent ev(WindowEvent::Type::KeyDown);
        ev.key.keycode = kp->detail; // X11 keycode (not keysym)
        ev.key.repeat = false; // X11 doesn't distinguish easily
        push_event(ev);
        break;
    }

    case XCB_KEY_RELEASE: {
        auto* kr = reinterpret_cast<xcb_key_release_event_t*>(event);
        WindowEvent ev(WindowEvent::Type::KeyUp);
        ev.key.keycode = kr->detail;
        ev.key.repeat = false;
        push_event(ev);
        break;
    }

    case XCB_MOTION_NOTIFY: {
        auto* mn = reinterpret_cast<xcb_motion_notify_event_t*>(event);
        WindowEvent ev(WindowEvent::Type::MouseMove);
        ev.mouse_move.x = mn->event_x;
        ev.mouse_move.y = mn->event_y;
        ev.mouse_move.dx = mouse_tracked_ ? (mn->event_x - last_mouse_x_) : 0;
        ev.mouse_move.dy = mouse_tracked_ ? (mn->event_y - last_mouse_y_) : 0;
        push_event(ev);

        last_mouse_x_ = mn->event_x;
        last_mouse_y_ = mn->event_y;
        mouse_tracked_ = true;
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
            push_event(ev);
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
            push_event(ev);
        }
        break;
    }

    case XCB_FOCUS_IN: {
        WindowEvent ev(WindowEvent::Type::Focus);
        push_event(ev);
        break;
    }

    case XCB_FOCUS_OUT: {
        WindowEvent ev(WindowEvent::Type::Blur);
        push_event(ev);
        break;
    }
    }
}

VkSurfaceKHR XcbWindow::create_surface(VkInstance /*instance*/)
{
    // TODO(etape-2): implement with Volk once Vulkan loading is integrated
    throw std::runtime_error("Vulkan surface creation not yet implemented (requires Volk)");
}

#endif // __linux__
