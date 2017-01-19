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

/* Includes 
 * - System */
#include <criticalSection.h>
#include <interrupts.h>
#include <threading.h>
#include <heap.h>

/* Includes
 * - Library */
#include <assert.h>

/* Instantiate a new critical section
 * with allocation and resets it */
CriticalSection_t *CriticalSectionCreate(int Flags)
{
	/* Allocate */
	CriticalSection_t *CSection = 
		(CriticalSection_t*)kmalloc(sizeof(CriticalSection_t));

	/* Reset */
	CriticalSectionConstruct(CSection, Flags);

	/* Done! */
	return CSection;
}

/* Constructs an already allocated section 
 * by resetting it's datamembers and initializing
 * the lock */
void CriticalSectionConstruct(CriticalSection_t *Section, int Flags)
{
	/* Set initial stats */
	Section->Flags = Flags;
	Section->IntrState = 0;
	Section->References = 0;
	Section->Owner = 0xFFFFFFFF;

	/* Reset the spinlock */
	SpinlockReset(&Section->Lock);
}

/* Destroy and release resources,
 * the lock MUST NOT be held when this
 * is called, so make sure its not used */
void CriticalSectionDestroy(CriticalSection_t *Section)
{
	/* Uh, not yet I guess */
	_CRT_UNUSED(Section);
}

/* Enter a critical section, the critical
 * section supports reentrancy if set at creation */
void CriticalSectionEnter(CriticalSection_t *Section)
{
	/* Variables */
	IntStatus_t IrqState = 0;

	/* If thread already has lock */
	if (Section->References != 0
		&& Section->Owner == ThreadingGetCurrentThreadId()
		&& (Section->Flags & CRITICALSECTION_REENTRANCY))
	{
		Section->References++;
		return;
	}

	/* Disable interrupts while
	 * we wait for the lock as well */
	IrqState = InterruptDisable();

	/* Otherwise wait for the section to clear */
	SpinlockAcquire(&Section->Lock);

	/* Set our stats */
	Section->Owner = ThreadingGetCurrentThreadId();
	Section->References = 1;
	Section->IntrState = IrqState;
}

/* Leave a critical section, the lock is 
 * not neccesarily released if held by multiple 
 * entrances */
void CriticalSectionLeave(CriticalSection_t *Section)
{
	/* Sanity */
	assert(Section->References > 0);

	/* Reduce */
	Section->References--;

	/* Set stats */
	if (Section->References == 0)
	{
		/* Set no owner */
		Section->Owner = 0xFFFFFFFF;
		
		/* Release */
		SpinlockRelease(&Section->Lock);

		/* Restore interrupt state */
		InterruptRestoreState(Section->IntrState);
	}
}
