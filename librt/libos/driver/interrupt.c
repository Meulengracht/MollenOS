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

/* RegisterInterruptSource 
 * Allocates the given interrupt source for use by
 * the requesting driver, an id for the interrupt source
 * is returned. After a succesful register, OnInterrupt
 * can be called by the event-system */
UUId_t
RegisterInterruptSource(
    _In_ MCoreInterrupt_t *Interrupt,
    _In_ Flags_t Flags)
{
	// Sanitize input
	if (Interrupt == NULL) {
		return UUID_INVALID;
	}
	return Syscall_InterruptAdd(Interrupt, Flags);
}

/* UnregisterInterruptSource 
 * Unallocates the given interrupt source and disables
 * all events of OnInterrupt */
OsStatus_t
UnregisterInterruptSource(
    _In_ UUId_t Source)
{
	// Sanitize input
	if (Source == UUID_INVALID) {
		return OsError;
	}
	return Syscall_InterruptRemove(Source);
}
