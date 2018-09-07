/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS - Vioarr Window Compositor System
 *  - The window compositor system and general window manager for
 *    MollenOS.
 */
#pragma once

#include <condition_variable>
#include <mutex>

class CSemaphore {
public:
    CSemaphore()    = default;
    ~CSemaphore()   = default;

    void Signal() {
        std::unique_lock<std::mutex> l(m_mtx);
        m_cnd.notify_one();
    }

    void Wait() {
        std::unique_lock<std::mutex> l(m_mtx);
        m_cnd.wait(l);
    }

    void WaitFor(std::chrono::milliseconds mils) {
        std::unique_lock<std::mutex> l(m_mtx);
        m_cnd.wait_for(l, mils);
    }

private:
    std::mutex              m_mtx;
    std::condition_variable m_cnd;
};
