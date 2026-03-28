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

#pragma once

#include <signal/signal.hpp>
#include <condition_variable>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace LoggingLib
{

    //! Message severity levels.
    enum class Severity {
        Debug, //!< Verbose diagnostic information.
        Info, //!< Normal operational messages.
        Warning, //!< Potential issues that do not prevent operation.
        Error, //!< Failures that prevent a specific operation.
        Fatal //!< Unrecoverable failures — the application will terminate.
    };

    //! A single log message with severity and text.
    struct LogMessage {
        Severity severity{Severity::Info}; //!< Severity level.
        std::string text; //!< Message content.
    };

    /*!
        Thread-safe logger that writes messages on a background thread.

        Uses SignalsLib::Signal<LogMessage> as the internal queue. Any thread
        can call logDebug(), logInfo(), etc. The background worker drains
        the queue and writes to stdout (Debug, Info) or stderr (Warning,
        Error, Fatal).

        RAII lifecycle: std::jthread auto-joins in the destructor and
        signals the stop_token to wake the worker.
    */
    class Logger {
    public:
        //! Spawn the background worker thread.
        Logger();

        //! Destructor — std::jthread auto-joins and signals stop.
        ~Logger();

        //! Non-copyable, non-movable (owns a thread).
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;
        Logger(Logger&&) = delete;
        Logger& operator=(Logger&&) = delete;

        //! Log a debug message.
        void logDebug(std::string_view message);

        //! Log an informational message.
        void logInfo(std::string_view message);

        //! Log a warning message.
        void logWarning(std::string_view message);

        //! Log an error message.
        void logError(std::string_view message);

        //! Log a fatal error message. Writes directly to stderr (synchronous).
        void logFatal(std::string_view message);

    private:
        //! Push a message onto the queue and wake the worker.
        void enqueue(Severity severity, std::string_view message);

        //! Worker thread entry point — drains the queue until stop is requested.
        void workerLoop(std::stop_token stop_token);

        SignalsLib::Signal<LogMessage> m_queue; //!< Thread-safe message queue.
        std::mutex m_mutex; //!< Protects the wake-up condition.
        std::condition_variable_any m_cv; //!< Wakes the worker when messages arrive.
        std::jthread m_worker; //!< Background writer thread (auto-joins on destruction).
    };

} // namespace LoggingLib
