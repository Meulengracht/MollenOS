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
 * - System */
#include "../arch/arch.h"
#include <os/spinlock.h>

/* Definitions */
#define CRITICALSECTION_PLAIN			0x0
#define CRITICALSECTION_REENTRANCY		0x1

/* Structures */
typedef struct _CriticalSection
{
	/* Settings */
	int Flags;

	/* Owner */
	UUId_t Owner;

	/* References */
	size_t References;

	/* Interrupt Status */
	IntStatus_t IntrState;

	/* Spinlock */
	Spinlock_t Lock;

} CriticalSection_t;

/* Prototypes */

/* Instantiate a new critical section
 * with allocation and resets it */
KERNELAPI CriticalSection_t *CriticalSectionCreate(int Flags);

/* Constructs an already allocated section 
 * by resetting it's datamembers and initializing
 * the lock */
KERNELAPI void CriticalSectionConstruct(CriticalSection_t *Section, int Flags);

/* Destroy and release resources,
 * the lock MUST NOT be held when this
 * is called, so make sure its not used */
KERNELAPI void CriticalSectionDestroy(CriticalSection_t *Section);

/* Enter a critical section, the critical
 * section supports reentrancy if set at creation */
KERNELAPI void CriticalSectionEnter(CriticalSection_t *Section);

/* Leave a critical section, the lock is 
 * not neccesarily released if held by multiple 
 * entrances */
KERNELAPI void CriticalSectionLeave(CriticalSection_t *Section);

#endif