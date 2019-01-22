/* ValiOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * ValiOS - Application Framework (Asgard)
 *  - Contains the implementation of the application framework used for building
 *    graphical applications.
 */

#include "include/semaphore.hpp"

namespace Asgard {
    void Semaphore::Signal() {
        std::unique_lock<std::mutex> l(m_mtx);
        m_cnd.notify_one();
    }

    void Semaphore::Wait() {
        std::unique_lock<std::mutex> l(m_mtx);
        m_cnd.wait(l);
    }

    void Semaphore::WaitFor(std::chrono::milliseconds mils) {
        std::unique_lock<std::mutex> l(m_mtx);
        m_cnd.wait_for(l, mils);
    }
}
