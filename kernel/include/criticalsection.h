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
 * MollenOS Synchronization
 *  - Critical Sections implementation used interrupt-disabling
 *    and spinlocks for synchronized access
 */

#ifndef _MCORE_CRITICAL_SECTION_
#define _MCORE_CRITICAL_SECTION_

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <os/spinlock.h>

/* Critical Section Definitions
 * Magic constants, bit definitions and types. */
#define CRITICALSECTION_PLAIN           0x0
#define CRITICALSECTION_REENTRANCY      0x1
typedef struct _CriticalSection {
    int                 Flags;
    UUId_t              Owner;
    int                 References;
    IntStatus_t         State;
    Spinlock_t          Lock;
} CriticalSection_t;

/* CriticalSectionCreate
 * Instantiate a new critical section with allocation and resets it */
KERNELAPI
CriticalSection_t*
KERNELABI
CriticalSectionCreate(
    _In_ int Flags);

/* CriticalSectionConstruct
 * Constructs an already allocated section by resetting it to default state. */
KERNELAPI
void
KERNELABI
CriticalSectionConstruct(
    _In_ CriticalSection_t *Section,
    _In_ int Flags);

/* CriticalSectionDestroy
 * Cleans up the lock by freeing resources allocated. */
KERNELAPI
void
KERNELABI
CriticalSectionDestroy(
    _In_ CriticalSection_t *Section);

/* CriticalSectionEnter
 * Enters the critical state, the call will block untill lock is granted. */
KERNELAPI
OsStatus_t
KERNELABI
CriticalSectionEnter(
    _In_ CriticalSection_t *Section);

/* CriticalSectionLeave
 * Leave a critical section. The lock is only released on reaching
 * 0 references. */
KERNELAPI
OsStatus_t
KERNELABI
CriticalSectionLeave(
    _In_ CriticalSection_t *Section);

#endif
