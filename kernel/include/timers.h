/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Timer Mangement Interface
 * - Contains the timer interface system and its implementation
 *   keeps track of system timers
 */

#ifndef _MCORE_TIMERS_H_
#define _MCORE_TIMERS_H_

/* Includes
 * - System */
#include <os/osdefs.h>
#include <time.h>

/* MCoreTimePerformanceOps
 * The two kinds of time-operations timers can
 * support for high performance timers. */
typedef struct _MCoreTimePerformanceOps {
    void                (*ReadFrequency)(_Out_ LargeInteger_t *Value);
    void                (*ReadTimer)(_Out_ LargeInteger_t *Value);
    void                (*ReadSystemTime)(_Out_ struct tm *SystemTime);
} MCoreTimePerformanceOps_t;

/* MCoreTimer
 * The timer structure, contains information about
 * the owner, the timeout and the type of timer. */
typedef struct _MCoreTimer {
    UUId_t               Id;
    UUId_t               AshId;
    __CONST void        *Data;

    size_t               Interval;
    volatile size_t      Current;
    int                  Periodic;
} MCoreTimer_t;

/* MCoreSystemTimer
 * The system timer structure
 * Contains information related to the registered system timers */
typedef struct _MCoreSystemTimer {
	UUId_t					Source;
	size_t					Tick;
	size_t					Ticks;
    clock_t                 (*SystemTick)(void);
} MCoreSystemTimer_t;

/* TimersInitialize
 * Initializes the timer sub-system that supports
 * registering of system timers and callback timers */
KERNELAPI
void
KERNELABI
TimersInitialize(void);

/* TimersRegisterSystemTimer 
 * Registrates a interrupt timer source with the
 * timer management, which keeps track of which interrupts
 * are available for time-keeping */
KERNELAPI
OsStatus_t
KERNELABI
TimersRegisterSystemTimer(
	_In_ UUId_t Source,
    _In_ size_t TickNs,
    _In_ clock_t (*SystemTickHandler)(void));

/* TimersRegisterPerformanceTimer
 * Registers a high performance timer that can be seperate
 * from the system timer. */
KERNELAPI
OsStatus_t
KERNELABI
TimersRegisterPerformanceTimer(
	_In_ MCoreTimePerformanceOps_t *Operations);

/* TimersRegisterClock
 * Registers a new time clock source. Must use the standard
 * C library definitions of time. */
KERNELAPI
OsStatus_t
KERNELABI
TimersRegisterClock(
	_In_ void (*SystemTimeHandler)(struct tm *SystemTime));
    
/* TimersStart 
 * Creates a new standard timer for the requesting process. */
KERNELAPI
UUId_t
KERNELABI
TimersStart(
    _In_ size_t IntervalNs,
    _In_ int Periodic,
    _In_ __CONST void *Data);

/* TimersStop
 * Destroys a existing standard timer, owner must be the requesting
 * process. Otherwise access fault. */
KERNELAPI
OsStatus_t
KERNELABI
TimersStop(
    _In_ UUId_t TimerId);

/* TimersInterrupt
 * Called by the interrupt-code to tell the timer-management system
 * a new interrupt has occured from the given source. This allows
 * the timer-management system to tell us if that was the active
 * timer-source */
KERNELAPI
OsStatus_t
KERNELABI
TimersInterrupt(
    _In_ UUId_t Source);

/* TimersGetSystemTime
 * Retrieves the system time. This is only ticking
 * if a system clock has been initialized. */
KERNELAPI
OsStatus_t
KERNELABI
TimersGetSystemTime(
    _Out_ struct tm *SystemTime);

/* TimersGetSystemTick 
 * Retrieves the system tick counter. This is only ticking
 * if a system timer has been initialized. */
KERNELAPI
OsStatus_t
KERNELABI
TimersGetSystemTick(
    _Out_ clock_t *SystemTick);

/* TimersQueryPerformanceFrequency
 * Returns how often the performance timer fires every
 * second, the value will never be 0 */
KERNELAPI
OsStatus_t
KERNELABI
TimersQueryPerformanceFrequency(
	_Out_ LargeInteger_t *Frequency);

/* TimersQueryPerformanceTick 
 * Retrieves the system performance tick counter. This is only ticking
 * if a system performance timer has been initialized. */
KERNELAPI
OsStatus_t
KERNELABI
TimersQueryPerformanceTick(
    _Out_ LargeInteger_t *Value);

#endif // !_MCORE_TIMERS_H_
