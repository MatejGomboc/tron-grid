/*
    TronGrid — thread-safe Signal<T> message queue
    Copyright (C) 2026 Matej Gomboc
    SPDX-Licence-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>

namespace SignalLib
{

    //! thread-safe, typed message queue for inter-system communication.
    //!
    //! ownership model:
    //! - receiver owns: std::shared_ptr<Signal<T>>
    //! - sender holds:  std::weak_ptr<Signal<T>>
    //!
    //! when the receiver is destroyed, the shared_ptr dies, the weak_ptr expires,
    //! and the sender knows to stop — no dangling pointers, no manual unregistration.
    template <typename T> class Signal {
    public:
        //! thread-safe enqueue.
        void emit(const T& data)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pending.push(data);
        }

        //! thread-safe dequeue. returns true if a value was consumed.
        [[nodiscard]] bool consume(T& out)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_pending.empty()) {
                return false;
            }
            out = std::move(m_pending.front());
            m_pending.pop();
            return true;
        }

        //! returns true if the queue is empty.
        [[nodiscard]] bool empty() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_pending.empty();
        }

        //! returns the number of pending messages.
        [[nodiscard]] std::size_t size() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_pending.size();
        }

    private:
        std::queue<T> m_pending; //!< queued messages
        mutable std::mutex m_mutex; //!< protects the queue
    };

} // namespace SignalLib
