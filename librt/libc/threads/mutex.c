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
#include <os/spinlock.h>
#include <os/syscall.h>

/* Includes
 * - Library */
#include <threads.h>
#include <stddef.h>
#include <stdlib.h>
#include <time.h>

/* Mutex Definitions
 * Definitions, constants and typedefs for mutex. */
#define MUTEX_COUNT         512

/* Mutex (Private)
 * Representation of an mutex in mollenos. */
static Spinlock_t MutexTableLock = SPINLOCK_INIT;
static struct __mutex {
    int             Type;
    thrd_t          Blocker;
    _Atomic(int)    Blocks;
    Spinlock_t      Lock;
} *MutexTable[MUTEX_COUNT] = { 0 };

/* mtx_init
 * Creates a new mutex object with type. The object pointed to by mutex is set to an 
 * identifier of the newly created mutex. */
int
mtx_init(
    _In_ mtx_t* mutex,
    _In_ int type)
{
    // Variables
    struct __mutex *mtx = NULL;
    mtx_t i;

    // Find an avaiable id
    SpinlockAcquire(&MutexTableLock);
    for (i = 0; i < MUTEX_COUNT; i++) {
        if (MutexTable[i] == NULL) {
            *mutex = i;
            break;
        }
    }
    SpinlockRelease(&MutexTableLock);
    if (i == MUTEX_COUNT) {
        return thrd_error;
    }

    // Allocate and initialize a mutex
    MutexTable[i] = mtx = (struct __mutex*)malloc(sizeof(struct __mutex));
    SpinlockReset(&mtx->Lock);
    mtx->Type = type;
    mtx->Blocker = UUID_INVALID;
    mtx->Blocks = ATOMIC_VAR_INIT(0);
    return thrd_success;
}

/* mtx_destroy
 * Destroys the mutex pointed to by mutex. If there are threads waiting on mutex, 
 * the behavior is undefined. */
void
mtx_destroy(
    _In_ mtx_t *mutex)
{
    // Sanitize input
    if (*mutex >= MUTEX_COUNT || MutexTable[*mutex] == NULL) {
        return;
    }

    // Release lock, free resources
    SpinlockRelease(&MutexTable[*mutex]->Lock);
    free(MutexTable[*mutex]);
    MutexTable[*mutex] = NULL;
}

/* mtx_trylock
 * Tries to lock the mutex pointed to by mutex without blocking. 
 * Returns immediately if the mutex is already locked. */
int
mtx_trylock(
    _In_ mtx_t *mutex)
{
    // Variables
    struct __mutex *mtx = NULL;

    // Sanitize input
    if (*mutex >= MUTEX_COUNT || MutexTable[*mutex] == NULL) {
        return thrd_error;
    }

    // Instantiate pointer
    mtx = MutexTable[*mutex];

    // If this thread already holds the mutex,
    // increase ref count, but only if we're recursive 
    if (mtx->Type & mtx_recursive) {
        if (atomic_load(&mtx->Blocks) != 0 && mtx->Blocker == thrd_current()) {
            atomic_fetch_add(&mtx->Blocks, 1);
            return thrd_success;
        }
    }

    // Try acquring the
    if (SpinlockTryAcquire(&mtx->Lock) == OsError) {
        return thrd_busy;
    }
    mtx->Blocker = thrd_current();
    atomic_store(&mtx->Blocks, 1);
    return thrd_success;
}

/* mtx_lock 
 * Blocks the current thread until the mutex pointed to by mutex is locked.
 * The behavior is undefined if the current thread has already locked the mutex 
 * and the mutex is not recursive. */
int
mtx_lock(
    _In_ mtx_t* mutex)
{
    // Variables
    struct __mutex *mtx = NULL;

    // Sanitize input
    if (*mutex >= MUTEX_COUNT || MutexTable[*mutex] == NULL) {
        return thrd_error;
    }

    // Instantiate pointer
    mtx = MutexTable[*mutex];

    // If this thread already holds the mutex,
    // increase ref count, but only if we're recursive 
    if (mtx->Type & mtx_recursive) {
        if (atomic_load(&mtx->Blocks) != 0 && mtx->Blocker == thrd_current()) {
            atomic_fetch_add(&mtx->Blocks, 1);
            return thrd_success;
        }
    }

    // acquire lock and set information
    SpinlockAcquire(&mtx->Lock);
    mtx->Blocker = thrd_current();
    atomic_store(&mtx->Blocks, 1);
    return thrd_success;
}

/* mtx_timedlock
 * Blocks the current thread until the mutex pointed to by mutex is 
 * locked or until the TIME_UTC based time point pointed to by time_point has been reached. */
int
mtx_timedlock(
    _In_ mtx_t *restrict mutex,
    _In_ __CONST struct timespec *restrict time_point)
{
    // Variables
    struct __mutex *mtx = NULL;
    struct timespec tstamp;

    // Sanitize input
    if (*mutex >= MUTEX_COUNT || MutexTable[*mutex] == NULL) {
        return thrd_error;
    }

    // Instantiate pointer
    mtx = MutexTable[*mutex];

    // If this thread already holds the mutex,
    // increase ref count, but only if we're recursive 
    if (mtx->Type & mtx_recursive) {
        if (atomic_load(&mtx->Blocks) != 0 && mtx->Blocker == thrd_current()) {
            atomic_fetch_add(&mtx->Blocks, 1);
            return thrd_success;
        }
    }

    // Wait with timeout
    while (SpinlockTryAcquire(&mtx->Lock) == OsError) {
        timespec_get(&tstamp, TIME_UTC);
        if (tstamp.tv_sec > time_point->tv_sec) {
            return thrd_timedout;
        }
        if (tstamp.tv_sec == time_point->tv_sec
            && tstamp.tv_nsec >= time_point->tv_nsec) {
            return thrd_timedout;
        }
        thrd_yield();
    }

    mtx->Blocker = thrd_current();
    atomic_store(&mtx->Blocks, 1);
    return thrd_success;
}

/* mtx_unlock
 * Unlocks the mutex pointed to by mutex. */
int
mtx_unlock(
    _In_ mtx_t *mutex)
{
    // Variables
    struct __mutex *mtx = NULL;
    int Blocks = 0;

    // Sanitize input
    if (*mutex >= MUTEX_COUNT || MutexTable[*mutex] == NULL) {
        return thrd_error;
    }

    // Instantiate pointer
    mtx = MutexTable[*mutex];

    // Sanitize unlock status
    if (atomic_load(&mtx->Blocks) == 0) {
        return thrd_error;
    }
    Blocks = atomic_fetch_sub(&mtx->Blocks, 1);
    Blocks--;
    if (Blocks == 0) {
        mtx->Blocker = UUID_INVALID;
        SpinlockRelease(&mtx->Lock);
    }
    return thrd_success;
}
