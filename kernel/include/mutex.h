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
 * Synchronization (Mutex)
 * - Hybrid mutex implementation. Contains a spinlock that serves
 *   as the locking primitive, with extended block capabilities.
 */

#ifndef __VALI_MUTEX_H__
#define __VALI_MUTEX_H__

#include <os/osdefs.h>
#include <os/spinlock.h>
#include <ds/collection.h>

// We support recursive mutexes
#define MUTEX_RECURSIVE SPINLOCK_RECURSIVE

typedef struct {
    Collection_t BlockQueue;
    Spinlock_t   SyncObject;
} Mutex_t;

#define MUTEX_INIT(Flags) { COLLECTION_INIT(KeyId), SPINLOCK_INIT(Flags) }

/* MutexTryLock
 * Tries to acquire the lock, does not block and returns immediately. */
KERNELAPI OsStatus_t KERNELABI
MutexTryLock(
    _In_ Mutex_t* Mutex);

/* MutexLock
 * Locks the mutex, blocks untill the lock is acquired. */
KERNELAPI void KERNELABI
MutexLock(
    _In_ Mutex_t* Mutex);

/* MutexUnlock
 * Release a lock on the given mutex. */
KERNELAPI void KERNELABI
MutexUnlock(
    _In_ Mutex_t* Mutex);

#endif //!__VALI_MUTEX_H__
