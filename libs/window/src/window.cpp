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

#include "window/window.hpp"

#ifdef _WIN32
#include "win32_window.hpp"
#elif defined(__linux__)
#include "xcb_window.hpp"
#endif

#include <cstdlib>

namespace WindowLib
{

    std::unique_ptr<Window> create(const WindowConfig& config, LoggingLib::Logger& logger)
    {
#ifdef _WIN32
        return std::make_unique<Win32Window>(config, logger);
#elif defined(__linux__)
        return std::make_unique<XcbWindow>(config, logger);
#else
        logger.logFatal("Unsupported platform.");
        std::abort();
        return nullptr;
#endif
    }

} // namespace WindowLib
