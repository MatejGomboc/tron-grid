#pragma once

#ifdef __linux__

#include "window.hpp"
#include <xcb/xcb.h>

class XcbWindow : public Window {
public:
    explicit XcbWindow(const WindowConfig& config);
    ~XcbWindow() override;

    void pump_events() override;
    VkSurfaceKHR create_surface(VkInstance instance) override;

    xcb_connection_t* connection() const
    {
        return connection_;
    }
    xcb_window_t handle() const
    {
        return window_;
    }

private:
    void handle_event(xcb_generic_event_t* event);

    xcb_connection_t* connection_ = nullptr;
    xcb_screen_t* screen_ = nullptr;
    xcb_window_t window_ = 0;

    // Atoms for window manager communication
    xcb_atom_t wm_protocols_ = 0;
    xcb_atom_t wm_delete_window_ = 0;

    // Mouse tracking
    int32_t last_mouse_x_ = 0;
    int32_t last_mouse_y_ = 0;
    bool mouse_tracked_ = false;
};

#endif // __linux__
