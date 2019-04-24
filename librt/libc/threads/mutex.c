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
 * Mutex Support Definitions & Structures
 * - This header describes the base mutex-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <os/spinlock.h>
#include <threads.h>
#include <time.h>

int
mtx_init(
    _In_ mtx_t* mutex,
    _In_ int    type)
{
    return MutexInitialize((Mutex_t*)mutex, type);
}

void
mtx_destroy(
    _In_ mtx_t* mutex)
{
    MutexDestroy((Mutex_t*)mutex);
}

int
mtx_trylock(
    _In_ mtx_t* mutex)
{
    return MutexTryAcquire((Mutex_t*)mutex);
}

int
mtx_lock(
    _In_ mtx_t* mutex)
{
    return MutexAcquire((Mutex_t*)mutex);
}

int
mtx_timedlock(
    _In_ mtx_t* restrict                 mutex,
    _In_ const struct timespec* restrict time_point)
{
    // Implement this functionality on top of the base mutex implementation
    // as it requires operating system support
    thrd_t          current_thrd = thrd_current();
    struct timespec tstamp;
    
    if (mutex == NULL || !(mutex->_flags & mtx_timed)) {
        return thrd_error;
    }

    // If this thread already holds the mutex,
    // increase ref count, but only if we're recursive 
    if (mutex->_flags & mtx_recursive) {
        while (1) {
            int initialcount = atomic_load(&mutex->_count);
            if (initialcount != 0 && mutex->_owner == current_thrd) {
                if (atomic_compare_exchange_weak(&mutex->_count, &initialcount, initialcount + 1)) {
                    return thrd_success;
                }
                continue;
            }
            break;
        }
    }

    // Wait with timeout
    while (SpinlockTryAcquire(&mutex->_syncobject) == OsError) {
        timespec_get(&tstamp, TIME_UTC);
        if (tstamp.tv_sec > time_point->tv_sec) {
            return thrd_timedout;
        }
        if (tstamp.tv_sec  == time_point->tv_sec && 
            tstamp.tv_nsec >= time_point->tv_nsec) {
            return thrd_timedout;
        }
        thrd_yield();
    }

    mutex->_owner = current_thrd;
    atomic_store(&mutex->_count, 1);
    return thrd_success;
}

int
mtx_unlock(
    _In_ mtx_t* mutex)
{
    return MutexRelease((Mutex_t*)mutex);
}
