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

#include <functional>
#include <thread>
#include <list>
#include "semaphore.hpp"

class CEventQueue {
    struct CEvent {
        std::function<void()>   Fn;
        std::size_t             TimeLeft;
        std::size_t             Period;
    };

public:
	static CEventQueue& GetInstance() {
		// Guaranteed to be destroyed.
		// Is instantiated on first use
		static CEventQueue _Instance;
		return _Instance;
	}
private:
	CEventQueue();

public:
	CEventQueue(CEventQueue const&)     = delete;
	void operator=(CEventQueue const&)  = delete;

    void AddPeriodic(std::function<void()> Fn, std::size_t Period);

private:
    void EventLoop();

private:
    std::thread*        m_Thread;
    std::list<CEvent*>  m_Events;
    CSemaphore          m_Semaphore;
};

// Shorthand for the vioarr
#define sEvents CEventQueue::GetInstance()
