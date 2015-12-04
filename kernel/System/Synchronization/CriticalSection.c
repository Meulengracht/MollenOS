/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* Critical Sections
*/

/* Includes */
#include <CriticalSection.h>
#include <Heap.h>
#include <Log.h>

/* CLib */
#include <assert.h>

/* Instantiate a new critical section */
CriticalSection_t *CriticalSectionCreate(void)
{
	/* Allocate */
	CriticalSection_t *CSection = 
		(CriticalSection_t*)kmalloc(sizeof(CriticalSection_t));

	/* Reset */
	CriticalSectionConstruct(CSection);

	/* Done! */
	return CSection;
}

/* Constructs an already allocated section */
void CriticalSectionConstruct(CriticalSection_t *Section)
{
	/* Set initial stats */
	Section->References = 0;
	Section->Owner = 0xFFFFFFFF;

	/* Reset the spinlock */
	SpinlockReset(&Section->Lock);
}

/* Destroy and release resources */
void CriticalSectionDestroy(CriticalSection_t *Section)
{
	_CRT_UNUSED(Section);
}

/* Access a critical section */
void CriticalSectionEnter(CriticalSection_t *Section)
{
	/* If thread already has lock */
	if (Section->References != 0
		&& Section->Owner == ThreadingGetCurrentThreadId())
	{
		Section->References++;
		return;
	}

	/* Otherwise wait for the section to clear */
	SpinlockAcquire(&Section->Lock);

	/* Set our stats */
	Section->Owner = ThreadingGetCurrentThreadId();
	Section->References = 1;
}

/* Leave a critical section */
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
	}
}