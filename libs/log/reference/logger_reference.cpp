#include "logger.h"

using namespace Engine;

Logger::~Logger()
{
    requestStop();
    waitForStop();
}

bool Logger::start(const std::string& log_file_name, std::string& out_error_message)
{
    out_error_message.clear();
    std::lock_guard lock(m_worker_thread_mutex);

    if (m_worker_thread_state != ThreadState::STOPPED) {
        out_error_message = "Log file is already open.";
        return false;
    }

    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }

    m_file.open(log_file_name, std::ofstream::out | std::ofstream::trunc);

    if (!m_file.is_open()) {
        out_error_message = "Failed to create log file.";
        return false;
    }

    m_worker_thread_state = ThreadState::STARTING;
    m_worker_thread = std::thread(logProcess, this);
    return true;
}

void Logger::logWrite(const std::string& message)
{
    {
        std::lock_guard lock(m_worker_thread_mutex);

        if (m_worker_thread_state != ThreadState::RUNNING) {
            return;
        }

        m_message_fifo.push(message);
    }

    m_worker_thread_wait_variable.notify_all();
}

void Logger::requestStop()
{
    {
        std::lock_guard lock(m_worker_thread_mutex);

        if ((m_worker_thread_state == ThreadState::STOPPING) || (m_worker_thread_state == ThreadState::STOPPED)) {
            return;
        }

        m_worker_thread_state = ThreadState::STOPPING;
    }

    m_worker_thread_wait_variable.notify_all();
}

void Logger::waitForStop()
{
    std::unique_lock<std::mutex> lock(m_worker_thread_mutex);

    m_stop_wait_variable.wait(lock, [=]() {
        return (m_worker_thread_state == ThreadState::STOPPED);
    });

    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }
}

void Logger::logProcess(Logger* logger)
{
    while (true) {
        std::unique_lock<std::mutex> lock(logger->m_worker_thread_mutex);

        logger->m_worker_thread_wait_variable.wait(lock, [logger]() {
            return (!logger->m_message_fifo.empty()) || (logger->m_worker_thread_state != ThreadState::RUNNING);
        });

        if (logger->m_worker_thread_state == ThreadState::STARTING) {
            logger->m_worker_thread_state = ThreadState::RUNNING;
        }

        if (!logger->m_message_fifo.empty()) {
            logger->m_file << logger->m_message_fifo.front() << std::endl;
            logger->m_message_fifo.pop();
        }

        if (logger->m_message_fifo.empty() && (logger->m_worker_thread_state != ThreadState::RUNNING)) {
            logger->m_worker_thread_state = ThreadState::STOPPED;
            lock.unlock();
            logger->m_stop_wait_variable.notify_all();
            return;
        }
    }
}
