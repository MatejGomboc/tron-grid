#pragma once

#include <queue>
#include <mutex>
#include <memory>

template <typename T> struct Signal {
    std::queue<T> pending;
    mutable std::mutex mutex;

    void emit(const T& data)
    {
        std::lock_guard<std::mutex> lock(mutex);
        pending.push(data);
    }

    bool consume(T& out)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (pending.empty())
            return false;
        out = std::move(pending.front());
        pending.pop();
        return true;
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return pending.empty();
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return pending.size();
    }
};

// Usage:
// - Receiver owns: std::shared_ptr<Signal<T>>
// - Sender holds:  std::weak_ptr<Signal<T>>
