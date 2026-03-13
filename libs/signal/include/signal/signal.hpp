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

namespace signals
{

    //! thread-safe, typed message queue for inter-system communication.
    //!
    //! ownership model:
    //! - receiver owns: std::shared_ptr<Signal<T>>
    //! - sender holds:  std::weak_ptr<Signal<T>>
    //!
    //! when the receiver is destroyed, the shared_ptr dies, the weak_ptr expires,
    //! and the sender knows to stop — no dangling pointers, no manual unregistration.
    template <typename T> struct Signal {
        std::queue<T> pending; //!< queued messages
        mutable std::mutex mutex; //!< protects the queue

        //! thread-safe enqueue.
        void emit(const T& data)
        {
            std::lock_guard<std::mutex> lock(mutex);
            pending.push(data);
        }

        //! thread-safe dequeue. returns true if a value was consumed.
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

        //! returns true if the queue is empty.
        bool empty() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return pending.empty();
        }

        //! returns the number of pending messages.
        std::size_t size() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return pending.size();
        }
    };

} // namespace signals
