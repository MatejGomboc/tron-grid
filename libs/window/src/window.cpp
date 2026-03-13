/*
 * TronGrid — window factory implementation
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#include "window/window.hpp"

#ifdef _WIN32
#include "win32_window.hpp"
#elif defined(__linux__)
#include "xcb_window.hpp"
#endif

#include <stdexcept>

namespace window
{

    std::unique_ptr<Window> create(const WindowConfig& config)
    {
#ifdef _WIN32
        return std::make_unique<Win32Window>(config);
#elif defined(__linux__)
        return std::make_unique<XcbWindow>(config);
#else
        throw std::runtime_error("Unsupported platform");
#endif
    }

} // namespace window
