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
 * MollenOS MCore - Interrupt Support Definitions & Structures
 * - This header describes the base interrupt-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

/* Includes
 * - System */
#include <os/driver/driver.h>
#include <os/syscall.h>

/* Includes
 * - Contracts */
#include <os/driver/contracts/timer.h>

/* RegisterInterruptSource 
 * Allocates the given interrupt source for use by
 * the requesting driver, an id for the interrupt source
 * is returned. After a succesful register, OnInterrupt
 * can be called by the event-system */
UUId_t RegisterInterruptSource(MCoreInterrupt_t *Interrupt, Flags_t Flags)
{
	/* Validate parameters */
	if (Interrupt == NULL) {
		return UUID_INVALID;
	}

	/* Redirect to system call */
	return (UUId_t)Syscall2(SYSCALL_REGISTERIRQ, SYSCALL_PARAM(Interrupt),
		SYSCALL_PARAM(Flags));
}

/* UnregisterInterruptSource 
 * Unallocates the given interrupt source and disables
 * all events of OnInterrupt */
OsStatus_t UnregisterInterruptSource(UUId_t Source)
{
	/* Validate parameters */
	if (Source == UUID_INVALID) {
		return OsError;
	}

	/* Redirect to system call */
	return (OsStatus_t)Syscall1(SYSCALL_REGISTERIRQ, SYSCALL_PARAM(Source));
}

/* RegisterSystemTimer
 * Registers the given interrupt source as a system
 * timer source, with the given tick. This way the system
 * can always keep track of timers */
OsStatus_t RegisterSystemTimer(UUId_t Interrupt, size_t NsPerTick)
{
	/* Validate parameters */
	if (Interrupt == UUID_INVALID
		|| NsPerTick == 0) {
		return OsError;
	}

	/* Redirect to system call */
	return (OsStatus_t)Syscall2(SYSCALL_REGISTERIRQ, SYSCALL_PARAM(Interrupt),
		SYSCALL_PARAM(NsPerTick));
}
