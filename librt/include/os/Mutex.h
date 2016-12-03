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
* MollenOS C Library - Standard Mutex
* Contains Mutex Synchronization Methods
*/

#ifndef __MUTEX_CLIB_H__
#define __MUTEX_CLIB_H__

/* C-Library - Includes */
#include <crtdefs.h>
#include <stdint.h>
#include <os/MollenOS.h>

/* Synchronizations */
#include <os/Spinlock.h>

/* CPP-Guard */
#ifdef __cplusplus
extern "C" {
#endif

/* Definitions */
#define MUTEX_INITIALIZOR		{0, 0, 0, 0}
#define MUTEX_PLAIN				0x0
#define MUTEX_RECURSIVE			0x1
#define MUTEX_DEFAULT_TIMEOUT	500
#define MUTEX_SUCCESS			0x0
#define MUTEX_BUSY				0x1

/***********************
 * Structures
 ***********************/

/* The mutex structure
 * used for exclusive access to a resource
 * between threads */
typedef struct _Mutex
{
	/* Mutex flags/type */
	int Flags;

	/* Task that is blocking */
	TId_t Blocker;

	/* Total amout of blocking */
	size_t Blocks;

	/* The spinlock */
	Spinlock_t Lock;

} Mutex_t;

/***********************
 * Mutex Prototypes
 ***********************/

/* Instantiates a new mutex of the given
 * type, it allocates all neccessary resources
 * as well. */
_MOS_API Mutex_t *MutexCreate(int Flags);

/* Instantiates a new mutex of the given
 * type, using pre-allocated memory */
_MOS_API void MutexConstruct(Mutex_t *Mutex, int Flags);

/* Destroys a mutex and frees resources
 * allocated by the mutex */
_MOS_API void MutexDestruct(Mutex_t *Mutex);

/* Lock a mutex, this is a
 * blocking call */
_MOS_API int MutexLock(Mutex_t *Mutex);

/* Tries to lock a mutex, if the 
 * mutex is locked, this returns 
 * MUTEX_BUSY, otherwise MUTEX_SUCCESS */
_MOS_API int MutexTryLock(Mutex_t *Mutex);

/* Tries to lock a mutex, with a timeout
 * which means it'll keep retrying locking
 * untill the time has passed */
_MOS_API int MutexTimedLock(Mutex_t *Mutex, time_t Expiration);

/* Unlocks a mutex, reducing the blocker
 * count by 1 if recursive, otherwise it opens
 * the mutex */
_MOS_API void MutexUnlock(Mutex_t *Mutex);

/* CPP Guard */
#ifdef __cplusplus
}
#endif

#endif //!__MUTEX_CLIB_H__