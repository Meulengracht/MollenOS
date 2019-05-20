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

#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/mollenos.h>
#include <os/futex.h>
#include <threads.h>
#include <time.h>

#define MUTEX_SPINS 1000

static SystemDescriptor_t SystemInfo = { 0 };

int
mtx_init(
    _In_ mtx_t* mutex,
    _In_ int    type)
{
    if (mutex == NULL) {
        return thrd_error;
    }

    // Get information about the system
    if (SystemInfo.NumberOfActiveCores == 0) {
        SystemQuery(&SystemInfo);
    }
    
    mutex->_flags = type;
    mutex->_owner = UUID_INVALID;
    mutex->_val   = ATOMIC_VAR_INIT(0);
    mutex->_refs  = ATOMIC_VAR_INIT(0);
    return thrd_success;
}

void
mtx_destroy(
    _In_ mtx_t* mutex)
{
    FutexParameters_t parameters;
    
    if (mutex != NULL) {
        parameters._futex0 = &mutex->_val;
        parameters._val0   = INT_MAX;
        parameters._flags  = FUTEX_WAKE_PRIVATE;
        Syscall_FutexWake(&parameters);
    }
}

int
mtx_trylock(
    _In_ mtx_t* mutex)
{
    int c;
    int z = 0;
    
    if (mutex == NULL) {
        return thrd_error;
    }
    
    // If this thread already holds the mutex,
    // increase ref count, but only if we're recursive 
    if (mutex->_flags & mtx_recursive) {
        while (1) {
            int initialcount = atomic_load(&mutex->_refs);
            if (initialcount != 0 && mutex->_owner == thrd_current()) {
                if (atomic_compare_exchange_weak(&mutex->_refs, &initialcount, initialcount + 1)) {
                    return thrd_success;
                }
                continue;
            }
            break;
        }
    }
    
    c = atomic_compare_exchange_strong(&mutex->_val, &z, 1);
    if (!c) {
        mutex->_owner = thrd_current();
        atomic_store(&mutex->_refs, 1);
        return thrd_success;
    }
    return thrd_busy;
}

int
mtx_lock(
    _In_ mtx_t* mutex)
{
    FutexParameters_t parameters;
    int z = 0;
    int c;
    
    if (mutex == NULL) {
        return thrd_error;
    }
    
    // If this thread already holds the mutex,
    // increase ref count, but only if we're recursive 
    if (mutex->_flags & mtx_recursive) {
        while (1) {
            int initialcount = atomic_load(&mutex->_refs);
            if (initialcount != 0 && mutex->_owner == thrd_current()) {
                if (atomic_compare_exchange_weak(&mutex->_refs, &initialcount, initialcount + 1)) {
                    return thrd_success;
                }
                continue;
            }
            break;
        }
    }
    
    parameters._futex0  = &mutex->_val;
    parameters._val0    = 2; // we always sleep on expecting a two
    parameters._timeout = 0;
    parameters._flags   = FUTEX_WAIT_PRIVATE;
    
    // Loop untill we get the lock
    c = atomic_compare_exchange_strong(&mutex->_val, &z, 1);
    if (c != 0) {
        if (c != 2) {
            c = atomic_exchange(&mutex->_val, 2);
        }
        while (c != 0) {
            Syscall_FutexWait(&parameters);
            c = atomic_exchange(&mutex->_val, 2);
        }
    }
    
    mutex->_owner = thrd_current();
    atomic_store(&mutex->_refs, 1);
    return thrd_success;
}

int
mtx_timedlock(
    _In_ mtx_t* restrict                 mutex,
    _In_ const struct timespec* restrict time_point)
{
    FutexParameters_t parameters;
    time_t            msec = 0;
	struct timespec   now, result;
    int               z = 0;
    int               c;
    
    if (mutex == NULL || !(mutex->_flags & mtx_timed)) {
        return thrd_error;
    }
    
    // If this thread already holds the mutex,
    // increase ref count, but only if we're recursive 
    if (mutex->_flags & mtx_recursive) {
        while (1) {
            int initialcount = atomic_load(&mutex->_refs);
            if (initialcount != 0 && mutex->_owner == thrd_current()) {
                if (atomic_compare_exchange_weak(&mutex->_refs, &initialcount, initialcount + 1)) {
                    return thrd_success;
                }
                continue;
            }
            break;
        }
    }
    
    // Calculate time to sleep
	timespec_get(&now, TIME_UTC);
    timespec_diff(time_point, &now, &result);
    msec = result.tv_sec * MSEC_PER_SEC;
    if (result.tv_nsec != 0) {
        msec += ((result.tv_nsec - 1) / NSEC_PER_MSEC) + 1;
    }
    
    parameters._futex0  = &mutex->_val;
    parameters._val0    = 2; // we always sleep on expecting a two
    parameters._timeout = msec;
    parameters._flags   = FUTEX_WAIT_PRIVATE;
    
    // Loop untill we get the lock
    c = atomic_compare_exchange_strong(&mutex->_val, &z, 1);
    if (c != 0) {
        if (c != 2) {
            c = atomic_exchange(&mutex->_val, 2);
        }
        while (c != 0) {
            if (Syscall_FutexWait(&parameters) == OsTimeout) {
                return thrd_timedout;
            }
            c = atomic_exchange(&mutex->_val, 2);
        }
    }
    
    mutex->_owner = thrd_current();
    atomic_store(&mutex->_refs, 1);
    return thrd_success;
}

int
mtx_unlock(
    _In_ mtx_t* mutex)
{
    FutexParameters_t parameters;
    int               initialcount;
    if (mutex == NULL) {
        return thrd_error;
    }

    // Sanitize state of the mutex, are we even able to unlock it?
    initialcount = atomic_load(&mutex->_refs);
    if (initialcount == 0 || mutex->_owner != thrd_current()) {
        return thrd_error;
    }
    
    initialcount = atomic_fetch_sub(&mutex->_refs, 1) - 1;
    if (initialcount == 0) {
        parameters._futex0  = &mutex->_val;
        parameters._val0    = 1;
        parameters._flags   = FUTEX_WAKE_PRIVATE;

        mutex->_owner = UUID_INVALID;
        if (atomic_fetch_sub(&mutex->_val, 1) != 1) {
            atomic_store(&mutex->_val, 0);
            Syscall_FutexWake(&parameters);
        }
    }
    return thrd_success;
}
