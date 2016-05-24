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
* MollenOS - Mutex Synchronization Functions
*/

/* Includes */
#include <os/MollenOS.h>
#include <os/Syscall.h>
#include <os/Thread.h>

/* C Library */
#include <stddef.h>
#include <stdlib.h>
#include <time.h>

#ifdef LIBC_KERNEL
void __MutexLibCEmpty(void)
{
}
#else

/* Instantiates a new mutex of the given
 * type, it allocates all neccessary resources
 * as well. */
Mutex_t *MutexCreate(int Flags)
{
	/* Allocate a new mutex instance */
	Mutex_t* Mutex = malloc(sizeof(Mutex_t));

	/* Reuse the construct
	 * function for cleverness points */
	MutexConstruct(Mutex, Flags);

	/* Done, return the mutex */
	return Mutex;
}

/* Instantiates a new mutex of the given
 * type, using pre-allocated memory */
void MutexConstruct(Mutex_t *Mutex, int Flags)
{
	/* Reset */
	Mutex->Flags = Flags;
	Mutex->Blocker = 0;
	Mutex->Blocks = 0;

	/* Reset spinlock */
	SpinlockReset(&Mutex->Lock);
}

/* Destroys a mutex and frees resources
 * allocated by the mutex */
void MutexDestruct(Mutex_t *Mutex)
{
	/* Make sure spinlock is released */
	SpinlockRelease(&Mutex->Lock);

	/* Free the mutex */
	free(Mutex);
}

/* Tries to lock a mutex, if the
 * mutex is locked, this returns
 * MUTEX_BUSY, otherwise MUTEX_SUCCESS */
int MutexTryLock(Mutex_t *Mutex)
{
	/* If this thread already holds the mutex,
	 * increase ref count, but only if we're recursive */
	if (Mutex->Blocks != 0
		&& Mutex->Blocker == ThreadGetCurrentId())
	{
		/* We must be recursive for this 
		 * property to hold true */
		if (Mutex->Flags & MUTEX_RECURSIVE) {
			Mutex->Blocks++;
			return MUTEX_SUCCESS;
		}
		else
			return MUTEX_BUSY;
	}

	/* Try to acquire the lock */
	if (SpinlockTryAcquire(&Mutex->Lock) == 0) {
		return MUTEX_BUSY;
	}

	/* Yay! We got the lock */
	Mutex->Blocks = 1;
	Mutex->Blocker = ThreadGetCurrentId();

	/* Done! */
	return MUTEX_SUCCESS;
}

/* Lock a mutex, this is a
 * blocking call */
int MutexLock(Mutex_t *Mutex)
{
	/* If this thread already holds the mutex,
	* increase ref count, but only if we're recursive */
	if (Mutex->Blocks != 0
		&& Mutex->Blocker == ThreadGetCurrentId())
	{
		/* We must be recursive for this
		* property to hold true */
		if (Mutex->Flags & MUTEX_RECURSIVE) {
			Mutex->Blocks++;
			return MUTEX_SUCCESS;
		}
		else
			return MUTEX_BUSY;
	}

	/* Acquire the lock */
	SpinlockAcquire(&Mutex->Lock);

	/* Yay! We got the lock */
	Mutex->Blocks = 1;
	Mutex->Blocker = ThreadGetCurrentId();

	/* Done! */
	return MUTEX_SUCCESS;
}

/* Tries to lock a mutex, with a timeout
 * which means it'll keep retrying locking
 * for the given time (Seconds) */
int MutexTimedLock(Mutex_t *Mutex, size_t Timeout)
{
	/* If this thread already holds the mutex,
	* increase ref count, but only if we're recursive */
	if (Mutex->Blocks != 0
		&& Mutex->Blocker == ThreadGetCurrentId())
	{
		/* We must be recursive for this
		* property to hold true */
		if (Mutex->Flags & MUTEX_RECURSIVE) {
			Mutex->Blocks++;
			return MUTEX_SUCCESS;
		}
		else
			return MUTEX_BUSY;
	}

	/* Busy-Loop */
	time_t Expire = time(NULL);
	Expire += Timeout;

	/* Wait for mutex to become free */
	while (SpinlockTryAcquire(&Mutex->Lock) == 0) {
		
		/* Get time now */
		time_t Current = time(NULL);

		/* Check if we are expired */
		if (Expire < Current)
			return MUTEX_BUSY;

		/* Yield.. */
		ThreadYield();
	}

	/* Yay! We got the lock */
	Mutex->Blocks = 1;
	Mutex->Blocker = ThreadGetCurrentId();

	/* Done! */
	return MUTEX_SUCCESS;
}

/* Unlocks a mutex, reducing the blocker
 * count by 1 if recursive, otherwise it opens
 * the mutex */
void MutexUnlock(Mutex_t *Mutex)
{
	/* Sanity */
	if (Mutex->Blocks == 0)
		return;

	/* Release one lock */
	Mutex->Blocks--;

	/* Are we done? */
	if (Mutex->Blocks == 0) {
		Mutex->Blocker = 0;
		SpinlockRelease(&Mutex->Lock);
	}
}

#endif