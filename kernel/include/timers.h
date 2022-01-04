/**
 * Copyright 2011, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Timer Mangement Interface
 * - Contains the timer interface system and its implementation
 *   keeps track of system timers
 */

#ifndef __VALI_TIMERS_H__
#define __VALI_TIMERS_H__

#include <os/osdefs.h>
#include <ds/list.h>
#include <time.h>

typedef struct SystemPerformanceTimerOps {
    void (*ReadFrequency)(LargeInteger_t*);
    void (*ReadTimer)(LargeInteger_t*);
} SystemPerformanceTimerOps_t;

/**
 * @brief Adds a number of seconds to the system wallclock
 *
 * @param[In] seconds The number of seconds to add to the wallclock
 */
KERNELAPI void KERNELABI
TimeWallClockAddTime(
        _In_ int seconds);

/* TimersRegisterPerformanceTimer
 * Registers a high performance timer that can be seperate from the system timer. */
KERNELAPI OsStatus_t KERNELABI
TimersRegisterPerformanceTimer(
    _In_ SystemPerformanceTimerOps_t* Operations);

/* TimersGetSystemTick 
 * Retrieves the system tick counter. This is only ticking
 * if a system timer has been initialized. */
KERNELAPI OsStatus_t KERNELABI
TimersGetSystemTick(
    _Out_ clock_t *SystemTick);

/* TimersQueryPerformanceFrequency
 * Returns how often the performance timer fires every
 * second, the value will never be 0 */
KERNELAPI OsStatus_t KERNELABI
TimersQueryPerformanceFrequency(
    _Out_ LargeInteger_t *Frequency);

/* TimersQueryPerformanceTick 
 * Retrieves the system performance tick counter. This is only ticking
 * if a system performance timer has been initialized. */
KERNELAPI OsStatus_t KERNELABI
TimersQueryPerformanceTick(
    _Out_ LargeInteger_t *Value);

#endif // !__VALI_TIMERS_H__
