/**
 * MollenOS
 *
 * <Copyright 2017, Philip Meulengracht
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

// We support recursive mutexes
#define MUTEX_PLAIN     0
#define MUTEX_RECURSIVE 0x1
#define MUTEX_TIMED     0x2

typedef struct {
    Flags_t      Flags;
    UUId_t       Owner;
    _Atomic(int) References;
    _Atomic(int) Value;
} Mutex_t;

#define OS_MUTEX_INIT(Flags) { Flags, UUID_INVALID, ATOMIC_VAR_INIT(0), ATOMIC_VAR_INIT(0) }

/**
 * * MutexConstruct
 * Initializes a mutex to default values, with the given configuration.
 */
KERNELAPI void KERNELABI
MutexConstruct(
    _In_ Mutex_t* Mutex,
    _In_ Flags_t  Configuration);

/**
 * * MutexDestruct 
 * Wakes up all threads in the wait queue and attempts to clear out. This can
 * fail and is not guaranteed to work. If it fails, this need to be tried again.
 */
KERNELAPI OsStatus_t KERNELABI
MutexDestruct(
    _In_ Mutex_t* Mutex);

/**
 * * MutexTryLock
 * Tries to acquire the lock, does not block and returns immediately.
 */
KERNELAPI OsStatus_t KERNELABI
MutexTryLock(
    _In_ Mutex_t* Mutex);

/**
 * * MutexLock
 * Locks the mutex, blocks untill the lock is acquired.
 */
KERNELAPI void KERNELABI
MutexLock(
    _In_ Mutex_t* Mutex);

/**
 * * MutexLockTimed
 * Tries to acquire the mutex in the period given, if it times out it 
 * returns OsTimeout. 
 */
KERNELAPI OsStatus_t KERNELABI
MutexLockTimed(
    _In_ Mutex_t* Mutex,
    _In_ size_t   Timeout);

/**
 * * MutexUnlock
 * Release a lock on the given mutex. 
 */
KERNELAPI void KERNELABI
MutexUnlock(
    _In_ Mutex_t* Mutex);

#endif //!__VALI_MUTEX_H__
