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

#include "event_queue.hpp"
#include <os/mollenos.h>

CEventQueue::CEventQueue()
{
    m_Thread = new std::thread(std::bind(&CEventQueue::EventLoop, this));
}

void CEventQueue::AddPeriodic(std::function<void()> Fn, std::size_t Period)
{
    CEvent* Event   = new CEvent;
    Event->Fn       = Fn;
    Event->Period   = Period;
    Event->TimeLeft = Period;
    m_Events.push_back(Event);
    m_Semaphore.Signal();
}

void CEventQueue::EventLoop()
{
    std::chrono::time_point<std::chrono::steady_clock> LastUpdate;
    bool IsRunning = true;
    SetCurrentThreadName("vioarr_events");

    while (IsRunning) {
        if (m_Events.size() == 0) {
            m_Semaphore.Wait();
        }

        // Sort by time-left before taking a new counter
        m_Events.sort([](CEvent* f, CEvent* s) { return f->TimeLeft < s->TimeLeft; });
        CEvent* Front = m_Events.front();

        // Wait for its time-left, but time how long we actually slept
        LastUpdate = std::chrono::steady_clock::now();
        m_Semaphore.WaitFor(std::chrono::milliseconds(Front->TimeLeft));
        
        auto MsSlept = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - LastUpdate);
        Front->TimeLeft -= std::min<std::size_t>(Front->TimeLeft, MsSlept.count());
        if (Front->TimeLeft == 0) {
            Front->Fn();
            Front->TimeLeft = Front->Period;
        }
    }
}
