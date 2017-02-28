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

#ifndef _MUTEX_INTERFACE_H_
#define _MUTEX_INTERFACE_H_

/* Includes
 * - System */
#include <os/osdefs.h>
#include <os/spinlock.h>

/* Mutex Definitions 
 * Magic constants and initializor constants for
 * the mutex interface */
#define MUTEX_INITIALIZOR		{0, 0, 0, 0}
#define MUTEX_PLAIN				0x0
#define MUTEX_RECURSIVE			0x1
#define MUTEX_DEFAULT_TIMEOUT	500
#define MUTEX_SUCCESS			0x0
#define MUTEX_BUSY				0x1

/* The mutex structure
 * used for exclusive access to a resource
 * between threads */
typedef struct _Mutex {
	Flags_t				Flags;
	UUId_t				Blocker;
	size_t				Blocks;
	Spinlock_t			Lock;
} Mutex_t;

/* Start one of these before function prototypes */
_CODE_BEGIN

/* MutexCreate
 * Instantiates a new mutex of the given
 * type, it allocates all neccessary resources
 * as well. */
MOSAPI 
Mutex_t *
MutexCreate(
	_In_ Flags_t Flags);

/* MutexConstruct
 * Instantiates a new mutex of the given
 * type, using pre-allocated memory */
MOSAPI 
OsStatus_t 
MutexConstruct(
	_In_ Mutex_t *Mutex, 
	_In_ Flags_t Flags);

/* MutexDestruct
 * Destroys a mutex and frees resources
 * allocated by the mutex */
MOSAPI 
OsStatus_t
MutexDestruct(
	_In_ Mutex_t *Mutex);

/* MutexLock
 * Lock a mutex, this is a blocking call */
MOSAPI 
int 
MutexLock(
	_In_ Mutex_t *Mutex);

/* MutexTryLock
 * Tries to lock a mutex, if the mutex is locked, this returns 
 * MUTEX_BUSY, otherwise MUTEX_SUCCESS */
MOSAPI 
int 
MutexTryLock(
	_In_ Mutex_t *Mutex);

/* MutexTimedLock
 * Tries to lock a mutex, with a timeout
 * which means it'll keep retrying locking
 * untill the time has passed */
MOSAPI 
int
MutexTimedLock(
	_In_ Mutex_t *Mutex, 
	_In_ time_t Expiration);

/* MutexUnlock
 * Unlocks a mutex, reducing the blocker
 * count by 1 if recursive, otherwise it opens
 * the mutex */
MOSAPI 
OsStatus_t
MutexUnlock(
	_In_ Mutex_t *Mutex);

_CODE_END

#endif //!_MUTEX_INTERFACE_H_
