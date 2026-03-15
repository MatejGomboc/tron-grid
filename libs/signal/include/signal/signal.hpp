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

#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>

namespace SignalLib
{

    /*!
        Thread-safe, typed message queue for inter-system communication.

        Ownership model:
        - Receiver owns: std::shared_ptr<Signal<T>>
        - Sender holds:  std::weak_ptr<Signal<T>>

        When the receiver is destroyed, the shared_ptr dies, the weak_ptr expires,
        and the sender knows to stop — no dangling pointers, no manual unregistration.
    */
    template <typename T> class Signal {
    public:
        //! Thread-safe enqueue.
        void emit(const T& data)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pending.push(data);
        }

        //! Thread-safe dequeue; returns true if a value was consumed.
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

        //! Returns true if the queue is empty.
        [[nodiscard]] bool empty() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_pending.empty();
        }

        //! Returns the number of pending messages.
        [[nodiscard]] std::size_t size() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_pending.size();
        }

    private:
        std::queue<T> m_pending; //!< Queued messages.
        mutable std::mutex m_mutex; //!< Protects the queue.
    };

} // namespace SignalLib
