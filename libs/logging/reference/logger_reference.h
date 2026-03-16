#pragma once

#include <condition_variable>
#include <fstream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace Engine {

class Logger {
public:
    ~Logger();
    bool start(const std::string& log_file_name, std::string& out_error_message);
    void logWrite(const std::string& message);
    void requestStop();
    void waitForStop();

private:
    enum class ThreadState {
        STOPPED,
        STARTING,
        RUNNING,
        STOPPING
    };

    static void logProcess(Logger* logger);

    std::ofstream m_file;
    std::queue<std::string> m_message_fifo;
    std::thread m_worker_thread;
    ThreadState m_worker_thread_state = ThreadState::STOPPED;
    std::mutex m_worker_thread_mutex;
    std::condition_variable m_worker_thread_wait_variable;
    std::condition_variable m_stop_wait_variable;
};

}  // namespace Engine
