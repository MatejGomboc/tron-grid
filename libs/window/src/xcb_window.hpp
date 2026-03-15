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

    //! XCB (X11) platform window implementation.
    class XcbWindow : public Window {
    public:
        explicit XcbWindow(const WindowConfig& config);
        ~XcbWindow() override;

        //! Polls pending XCB events into the event queue.
        void pumpEvents() override;

        //! Returns the xcb_window_t as a void pointer.
        [[nodiscard]] void* nativeHandle() const override;

        //! Returns the xcb_connection_t* as a void pointer.
        [[nodiscard]] void* nativeDisplay() const override;

    private:
        //! Dispatches a single XCB event into the event queue.
        void handleEvent(xcb_generic_event_t* event);

        xcb_connection_t* m_connection = nullptr; //!< XCB connection to the X server.
        xcb_screen_t* m_screen = nullptr; //!< Default screen.
        xcb_window_t m_window = 0; //!< XCB window identifier.

        xcb_atom_t m_wm_protocols = 0; //!< WM_PROTOCOLS atom for window manager communication.
        xcb_atom_t m_wm_delete_window = 0; //!< WM_DELETE_WINDOW atom for close event handling.

        int32_t m_last_mouse_x = 0; //!< Last known mouse x for delta computation.
        int32_t m_last_mouse_y = 0; //!< Last known mouse y for delta computation.
        bool m_mouse_tracked = false; //!< True after the first mouse event has been received.
    };

} // namespace WindowLib

#endif // __linux__
