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
* Mutexes
*/

/* Includes */
#include <Arch.h>
#include <Mutex.h>
#include <Scheduler.h>
#include <Heap.h>
#include <assert.h>

/* Allocates and initializes a mutex */
Mutex_t *MutexCreate(void)
{
	/* Allocate */
	Mutex_t* Mutex = kmalloc(sizeof(Mutex_t));
	
	/* Init */
	MutexConstruct(Mutex);

	/* Done */
	return Mutex;
}

/* Resets a mutex */
void MutexConstruct(Mutex_t *Mutex)
{
	/* Reset */
	Mutex->Blocker = 0;
	Mutex->Blocks = 0;
}

/* Destroys a mutex */
void MutexDestruct(Mutex_t *Mutex)
{
	/* Wake all remaining tasks waiting for this mutex */
	SchedulerWakeupAllThreads((Addr_t*)Mutex);

	/* Free resources */
	kfree(Mutex);
}

/* Get lock of mutex */
void MutexLock(Mutex_t *Mutex)
{
	/* If this thread already holds the mutex, increase ref count */
	if (Mutex->Blocks != 0 
		&& Mutex->Blocker == ThreadingGetCurrentThreadId())
	{
		Mutex->Blocks++;
		return;
	}

	/* Wait for mutex to become free */
	while (Mutex->Blocks != 0)
	{
		/* Wait for signal */
		SchedulerSleepThread((Addr_t*)Mutex);

		/* Yield */
		_ThreadYield();
	}

	/* Initialize */
	Mutex->Blocks = 1;
	Mutex->Blocker = ThreadingGetCurrentThreadId();
}

/* Release lock of mutex */
void MutexUnlock(Mutex_t *Mutex)
{
	/* Sanity */
	assert(Mutex->Blocks > 0);

	/* Release one lock */
	Mutex->Blocks--;

	/* Are we done? */
	if (Mutex->Blocks == 0)
		SchedulerWakeupOneThread((Addr_t*)Mutex);
}