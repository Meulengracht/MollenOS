/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
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
 * MollenOS MCore - Mutex Synchronization Primitve
 */

#ifndef _MCORE_MUTEX_H_
#define _MCORE_MUTEX_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>

/* Mutex Definitions 
 * Magic constants, settings and bit definitions. */
#define MUTEX_DEFAULT_TIMEOUT	500
typedef struct _Mutex {
	UUId_t              Blocker;
	size_t              Blocks;
    int                 Cleanup;
} Mutex_t;

/* MutexCreate
 * Allocates a new mutex and initializes it to default values. */
KERNELAPI
Mutex_t*
KERNELABI
MutexCreate(void);

/* MutexConstruct
 * Initializes an already allocated mutex-resource. */
KERNELAPI
void
KERNELABI
MutexConstruct(
    _In_ Mutex_t *Mutex);

/* MutexDestroy
 * Wakes up all sleepers on the mutex and frees resources. */
KERNELAPI
void
KERNELABI
MutexDestroy(
    _In_ Mutex_t *Mutex);
    
/* MutexTryLock
 * Tries to acquire the mutex-lock within the time-out value. */
KERNELAPI
OsStatus_t
KERNELABI
MutexTryLock(
    _In_ Mutex_t *Mutex,
    _In_ size_t Timeout);

/* MutexLock
 * Waits indefinitely for the mutex lock. */
KERNELAPI
OsStatus_t
KERNELABI
MutexLock(
    _In_ Mutex_t *Mutex);

/* MutexUnlock
 * Unlocks the mutex, by reducing lock-count by one. */
KERNELAPI
void
KERNELABI
MutexUnlock(
    _In_ Mutex_t *Mutex);

#endif // !_MCORE_MUTEX_H_
