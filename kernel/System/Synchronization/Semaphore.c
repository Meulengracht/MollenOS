/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* Semaphores
*/

/* Includes */
#include <assert.h>
#include <scheduler.h>
#include <semaphore.h>
#include <heap.h>

/* Externs */
extern void _yield(void);

/* Creates an semaphore */
Semaphore_t *SemaphoreCreate(int Value)
{
	Semaphore_t *Semaphore;

	/* Sanity */
	assert(Value >= 0);

	/* Allocate */
	Semaphore = (Semaphore_t*)kmalloc(sizeof(Semaphore_t));
	Semaphore->Value = Value;
	Semaphore->Creator = ThreadingGetCurrentThreadId();
	SpinlockReset(&Semaphore->Lock);

	return Semaphore;
}

void SemaphoreDestroy(Semaphore_t *Semaphore)
{
	/* Wake up all */
	SchedulerWakeupAllThreads((Addr_t*)Semaphore);

	/* Free it */
	kfree(Semaphore);
}

/* Acquire Lock */
void SemaphoreP(Semaphore_t *Semaphore)
{
	/* Lock */
	SpinlockAcquire(&Semaphore->Lock);

	Semaphore->Value--;
	if (Semaphore->Value < 0)
	{
		/* Important to release lock before we do this */
		SpinlockRelease(&Semaphore->Lock);
		SchedulerSleepThread((Addr_t*)Semaphore);
		
		_yield();
	}
	else
		SpinlockRelease(&Semaphore->Lock);
}

/* Release Lock */
void SemaphoreV(Semaphore_t *Semaphore)
{
	/* Lock */
	SpinlockAcquire(&Semaphore->Lock);

	/* Do Magic */
	Semaphore->Value++;
	if (Semaphore->Value <= 0)
		SchedulerWakeupOneThread((Addr_t*)Semaphore);

	/* Release */
	SpinlockRelease(&Semaphore->Lock);
}