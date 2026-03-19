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
        m_worker(&Logger::workerLoop, this)
    {
    }

    Logger::~Logger()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }

        m_cv.notify_one();
        m_worker.join();
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

    void Logger::workerLoop()
    {
        while (true) {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() {
                return m_stop || !m_queue.empty();
            });

            bool stopping = m_stop;
            lock.unlock();

            // Drain all pending messages.
            LogMessage msg;
            while (m_queue.consume(msg)) {
                std::string_view prefix = severityPrefix(msg.severity);
                if (msg.severity >= Severity::Warning) {
                    std::cerr << prefix << " " << msg.text << "\n";
                } else {
                    std::cout << prefix << " " << msg.text << "\n";
                }
            }

            if (stopping) {
                return;
            }
        }
    }

} // namespace LoggingLib
