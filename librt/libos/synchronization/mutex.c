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
 * MollenOS MCore - Mutex Support Definitions & Structures
 * - This header describes the base mutex-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

 /* Includes 
  * - System */
#include <os/thread.h>
#include <os/syscall.h>

/* Includes
 * - Library */
#include <stddef.h>
#include <stdlib.h>
#include <time.h>

/* MutexCreate
 * Instantiates a new mutex of the given
 * type, it allocates all neccessary resources
 * as well. */
Mutex_t *
MutexCreate(
	_In_ Flags_t Flags)
{
	/* Allocate a new mutex instance */
	Mutex_t* Mutex = malloc(sizeof(Mutex_t));

	/* Reuse the construct
	 * function for cleverness points */
	if (MutexConstruct(Mutex, Flags) != OsNoError) {
		free(Mutex);
		return NULL;
	}

	/* Done, return the mutex */
	return Mutex;
}

/* MutexConstruct
 * Instantiates a new mutex of the given
 * type, using pre-allocated memory */
OsStatus_t 
MutexConstruct(
	_In_ Mutex_t *Mutex, 
	_In_ Flags_t Flags)
{
	/* Reset */
	Mutex->Flags = Flags;
	Mutex->Blocker = 0;
	Mutex->Blocks = 0;

	/* Reset spinlock */
	return SpinlockReset(&Mutex->Lock);
}

/* MutexDestruct
 * Destroys a mutex and frees resources
 * allocated by the mutex */
OsStatus_t
MutexDestruct(
	_In_ Mutex_t *Mutex)
{
	/* Make sure spinlock is released 
	 * and free handle */
	SpinlockRelease(&Mutex->Lock);
	free(Mutex);
	return OsNoError;
}

/* MutexTryLock
 * Tries to lock a mutex, if the mutex is locked, this returns 
 * MUTEX_BUSY, otherwise MUTEX_SUCCESS */ 
int 
MutexTryLock(
	_In_ Mutex_t *Mutex)
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
		else {
			return MUTEX_BUSY;
		}
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

/* MutexLock
 * Lock a mutex, this is a blocking call */
int 
MutexLock(
	_In_ Mutex_t *Mutex)
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
		else {
			return MUTEX_BUSY;
		}
	}

	/* Acquire the lock */
	SpinlockAcquire(&Mutex->Lock);

	/* Yay! We got the lock */
	Mutex->Blocks = 1;
	Mutex->Blocker = ThreadGetCurrentId();

	/* Done! */
	return MUTEX_SUCCESS;
}

/* MutexTimedLock
 * Tries to lock a mutex, with a timeout
 * which means it'll keep retrying locking
 * untill the time has passed */
int
MutexTimedLock(
	_In_ Mutex_t *Mutex, 
	_In_ time_t Expiration)
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
		else {
			return MUTEX_BUSY;
		}
	}

	/* Wait for mutex to become free */
	while (SpinlockTryAcquire(&Mutex->Lock) == 0) {
		time_t Current = time(NULL);

		/* Check if we are expired */
		if (Expiration < Current) {
			return MUTEX_BUSY;
		}

		/* Yield.. */
		ThreadYield();
	}

	/* Yay! We got the lock */
	Mutex->Blocks = 1;
	Mutex->Blocker = ThreadGetCurrentId();

	/* Done! */
	return MUTEX_SUCCESS;
}

/* MutexUnlock
 * Unlocks a mutex, reducing the blocker
 * count by 1 if recursive, otherwise it opens
 * the mutex */
OsStatus_t
MutexUnlock(
	_In_ Mutex_t *Mutex)
{
	/* Sanitize blocks */
	if (Mutex->Blocks == 0) {
		return OsError;
	}

	/* Release one lock */
	Mutex->Blocks--;

	/* Are we done? */
	if (Mutex->Blocks == 0) {
		Mutex->Blocker = 0;
		return SpinlockRelease(&Mutex->Lock);
	}

	/* Otherwise just return */
	return OsNoError;
}
