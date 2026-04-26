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
        // Request stop. The std::condition_variable_any::wait overload that takes a
        // stop_token wakes the worker on request_stop() automatically — no manual
        // notify_one() needed. The jthread destructor then joins; the worker drains
        // remaining messages before returning.
        m_worker.request_stop();
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
        // The C++ memory model requires that any state read by the wait predicate be
        // modified under the same mutex used by the wait. Without this, a notification
        // emitted after the worker has checked the predicate (true → empty) but before
        // the worker has registered as a waiter inside cv.wait() is lost — the worker
        // would sleep indefinitely while messages pile up. Holding m_mutex around the
        // emit closes the race; notify_one() is intentionally outside the lock so the
        // woken worker doesn't immediately re-block on the mutex we just released.
        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_queue.emit({severity, std::string(message)});
        }
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
