/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
#include <ds/list.h>
#include <os/spinlock.h>

// We support recursive mutexes
#define MUTEX_FLAG_PLAIN     0
#define MUTEX_FLAG_RECURSIVE 0x1
#define MUTEX_FLAG_TIMED     0x2

typedef struct Mutex {
    _Atomic(unsigned int) flags;
    _Atomic(UUId_t)       owner;
    int                   referenceCount;
    list_t                blockQueue;
    spinlock_t            syncObject;
} Mutex_t;

#define OS_MUTEX_INIT(Flags) { ATOMIC_VAR_INIT(Flags), ATOMIC_VAR_INIT(UUID_INVALID), 0, LIST_INIT, SPINLOCK_INIT(spinlock_plain) }

/**
 * Initializes a mutex to default values.
 * @param mutex         [In] A pointer to a Mutex_t structure
 * @param configuration [In] Which kind of mutex to initialize
 */
KERNELAPI void KERNELABI
MutexConstruct(
    _In_ Mutex_t*     mutex,
    _In_ unsigned int configuration);

/**
 * Resets the mutex to default values and wakes any thread up waiting for the mutex.
 * The mutex is marked invalid and all threads waked will return OsInvalidParameters
 * @param mutex [In] A pointer to a Mutex_t structure
 */
KERNELAPI void KERNELABI
MutexDestruct(
    _In_ Mutex_t* mutex);

/**
 * Tries once to take the lock, and if it fails it does not block.
 * @param mutex [In] A pointer to a Mutex_t structure
 * @return
 */
KERNELAPI OsStatus_t KERNELABI
MutexTryLock(
    _In_ Mutex_t* mutex);

/**
 * Attemps to lock the mutex, this is a blocking operation.
 * @param mutex [In] A pointer to a Mutex_t structure
 */
KERNELAPI void KERNELABI
MutexLock(
    _In_ Mutex_t* mutex);

/**
 * Attemps to acquire the mutex in a given time-frame.
 * @param mutex   [In] A pointer to a Mutex_t structure
 * @param timeout [In] Timeout in milliseconds
 * @return        Returns OsTimeout if it failed to acquire within the timeout
 */
KERNELAPI OsStatus_t KERNELABI
MutexLockTimed(
    _In_ Mutex_t* mutex,
    _In_ size_t   timeout);

/**
 * Releases the given mutex
 * @param Mutex [In] A pointer to a Mutex_t structure
 */
KERNELAPI void KERNELABI
MutexUnlock(
    _In_ Mutex_t* mutex);

#endif //!__VALI_MUTEX_H__
