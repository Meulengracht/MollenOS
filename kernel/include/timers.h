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
#include <os/driver/timers.h>

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
} MCoreSystemTimer_t;

/* TimersInitialize
 * Initializes the timer sub-system that supports
 * registering of system timers and callback timers */
KERNELAPI
void
KERNELABI
TimersInitialize(void);

/* TimersRegistrate 
 * Registrates a interrupt timer source with the
 * timer management, which keeps track of which interrupts
 * are available for time-keeping */
KERNELAPI
OsStatus_t
KERNELABI
TimersRegister(
	_In_ UUId_t Source,
    _In_ size_t TickNs);
    
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

#endif // !_MCORE_TIMERS_H_
