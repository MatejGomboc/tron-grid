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

        RAII lifecycle: the constructor spawns the worker thread, and the
        destructor drains any remaining messages before joining.
    */
    class Logger {
    public:
        //! Spawn the background worker thread.
        Logger();

        //! Drain remaining messages and join the worker thread.
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

        //! Log a fatal error message.
        void logFatal(std::string_view message);

    private:
        //! Push a message onto the queue and wake the worker.
        void enqueue(Severity severity, std::string_view message);

        //! Worker thread entry point — drains the queue in a loop.
        void workerLoop();

        SignalsLib::Signal<LogMessage> m_queue; //!< Thread-safe message queue.
        std::thread m_worker; //!< Background writer thread.
        std::mutex m_mutex; //!< Protects the stop flag.
        std::condition_variable m_cv; //!< Wakes the worker when messages arrive or stop is requested.
        bool m_stop{false}; //!< Set to true when the logger is shutting down.
    };

} // namespace LoggingLib
