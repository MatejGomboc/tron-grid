/*
 * TronGrid — thread-safe Signal<T> message queue
 * Copyright (C) 2026 Matej Gomboc
 * SPDX-Licence-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>

namespace signal
{

    /// Thread-safe, typed message queue for inter-system communication.
    ///
    /// Ownership model:
    /// - Receiver owns: std::shared_ptr<Signal<T>>
    /// - Sender holds:  std::weak_ptr<Signal<T>>
    ///
    /// When the receiver is destroyed, the shared_ptr dies, the weak_ptr expires,
    /// and the sender knows to stop — no dangling pointers, no manual unregistration.
    template <typename T> struct Signal {
        std::queue<T> pending;
        mutable std::mutex mutex;

        /// Thread-safe enqueue.
        void emit(const T& data)
        {
            std::lock_guard<std::mutex> lock(mutex);
            pending.push(data);
        }

        /// Thread-safe dequeue. Returns true if a value was consumed.
        bool consume(T& out)
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (pending.empty()) {
                return false;
            }
            out = std::move(pending.front());
            pending.pop();
            return true;
        }

        /// Returns true if the queue is empty.
        bool empty() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return pending.empty();
        }

        /// Returns the number of pending messages.
        std::size_t size() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return pending.size();
        }
    };

} // namespace signal
