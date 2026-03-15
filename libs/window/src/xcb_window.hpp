/*
    TronGrid — XCB window implementation
    Copyright (C) 2026 Matej Gomboc
    SPDX-Licence-Identifier: GPL-3.0-or-later
*/

#pragma once

#ifdef __linux__

#include "window/window.hpp"
#include <xcb/xcb.h>

namespace WindowLib
{

class XcbWindow : public Window {
public:
    explicit XcbWindow(const WindowConfig& config);
    ~XcbWindow() override;

    void pumpEvents() override;
    void* nativeHandle() const override;
    void* nativeDisplay() const override;

private:
    void handleEvent(xcb_generic_event_t* event);

    xcb_connection_t* m_connection = nullptr;
    xcb_screen_t* m_screen = nullptr;
    xcb_window_t m_window = 0;

    // Atoms for window manager communication
    xcb_atom_t m_wm_protocols = 0;
    xcb_atom_t m_wm_delete_window = 0;

    // Mouse tracking
    int32_t m_last_mouse_x = 0;
    int32_t m_last_mouse_y = 0;
    bool m_mouse_tracked = false;
};

} // namespace WindowLib

#endif // __linux__
