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
 * MollenOS MCore - Timer Support Definitions & Structures
 * - This header describes the base timer-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

/* Includes
 * - System */
#include <os/driver/timers.h>
#include <os/syscall.h>

/* TimerStart 
 * Creates a new standard timer for the requesting process. 
 * When interval elapses a __TIMEOUT event is generated for
 * the owner of the timer. 
 * <Interval> is in MilliSeconds */
UUId_t
TimerStart(
    _In_ size_t Interval,
    _In_ int Periodic,
    _In_ __CONST void *Data)
{
    // Translate to Ns
    return Syscall_TimerCreate(Interval * 1000, Periodic, Data);
}

/* TimerStop
 * Destroys a existing standard timer, owner must be the requesting
 * process. Otherwise access fault. */
OsStatus_t
TimerStop(
    _In_ UUId_t TimerId) {
    return Syscall_TimerStop(TimerId);
}
