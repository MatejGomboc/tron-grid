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

#include "log/logger.hpp"
#include <iostream>

namespace LoggingLib
{

    //! Convert severity to a human-readable prefix string.
    [[nodiscard]] static std::string_view severityPrefix(Severity severity)
    {
        switch (severity) {
        case Severity::Debug:
            return "[DEBUG]";
        case Severity::Info:
            return "[INFO]";
        case Severity::Warning:
            return "[WARNING]";
        case Severity::Error:
            return "[ERROR]";
        case Severity::Fatal:
            return "[FATAL]";
        }
        return "[UNKNOWN]";
    }

    Logger::Logger() :
        m_worker([this](std::stop_token token) {
            workerLoop(token);
        })
    {
    }

    Logger::~Logger()
    {
        // Request stop and wake the worker. The jthread destructor
        // joins automatically — the worker drains remaining messages
        // before returning.
        m_worker.request_stop();
        m_cv.notify_one();
    }

    void Logger::logDebug(std::string_view message)
    {
        enqueue(Severity::Debug, message);
    }

    void Logger::logInfo(std::string_view message)
    {
        enqueue(Severity::Info, message);
    }

    void Logger::logWarning(std::string_view message)
    {
        enqueue(Severity::Warning, message);
    }

    void Logger::logError(std::string_view message)
    {
        enqueue(Severity::Error, message);
    }

    void Logger::logFatal(std::string_view message)
    {
        // Write directly to stderr — fatal messages must be visible
        // before std::abort(), so we bypass the async queue entirely.
        std::cerr << "[FATAL] " << message << "\n";
    }

    void Logger::enqueue(Severity severity, std::string_view message)
    {
        m_queue.emit({severity, std::string(message)});
        m_cv.notify_one();
    }

    void Logger::workerLoop(std::stop_token stop_token)
    {
        while (!stop_token.stop_requested()) {
            // Wait for messages or stop — the CV checks stop_token automatically
            {
                std::unique_lock<std::mutex> lock{m_mutex};
                m_cv.wait(lock, stop_token, [this]() {
                    return !m_queue.empty();
                });
            }
            // Lock released before draining — no lock ordering issue with Signal's mutex

            // Drain all pending messages.
            LogMessage msg{};
            while (m_queue.consume(msg)) {
                std::string_view prefix{severityPrefix(msg.severity)};
                if (msg.severity >= Severity::Warning) {
                    std::cerr << prefix << " " << msg.text << "\n";
                } else {
                    std::cout << prefix << " " << msg.text << "\n";
                }
            }
        }

        // Final drain — catch messages emitted between last check and stop
        LogMessage msg;
        while (m_queue.consume(msg)) {
            std::string_view prefix{severityPrefix(msg.severity)};
            if (msg.severity >= Severity::Warning) {
                std::cerr << prefix << " " << msg.text << "\n";
            } else {
                std::cout << prefix << " " << msg.text << "\n";
            }
        }
    }

} // namespace LoggingLib
