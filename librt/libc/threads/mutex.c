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
 * Mutex Support Definitions & Structures
 * - This header describes the base mutex-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/barrier.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/mollenos.h>
#include <os/futex.h>
#include <threads.h>
#include <time.h>

#define MUTEX_SPINS     1000
#define MUTEX_DESTROYED 0x1000

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
    
    mutex->flags = type;
    mutex->owner = UUID_INVALID;
    mutex->value = ATOMIC_VAR_INIT(0);
    mutex->references = ATOMIC_VAR_INIT(0);
    smp_wmb();
    
    return thrd_success;
}

void
mtx_destroy(
    _In_ mtx_t* mutex)
{
    FutexParameters_t parameters;
    
    if (mutex != NULL) {
        mutex->flags |= MUTEX_DESTROYED;
        smp_wmb();
        
        parameters._futex0 = &mutex->value;
        parameters._val0   = INT_MAX;
        parameters._flags  = FUTEX_WAKE_PRIVATE;
        Syscall_FutexWake(&parameters);
    }
}

int
mtx_trylock(
    _In_ mtx_t* mutex)
{
    int initialcount;
    int z = 0;
    int status;
    
    if (mutex == NULL) {
        return thrd_error;
    }
    
    // If this thread already holds the mutex,
    // increase ref count, but only if we're recursive
    if (mutex->flags & mtx_recursive) {
        while (1) {
            initialcount = atomic_load(&mutex->references);
            if (initialcount != 0 && mutex->owner == thrd_current()) {
                status = atomic_compare_exchange_weak_explicit(&mutex->references, 
                    &initialcount, initialcount + 1, memory_order_release,
                    memory_order_acquire);
                if (status) {
                    return thrd_success;
                }
                continue;
            }
            break;
        }
    }
    
    status = atomic_compare_exchange_strong(&mutex->value, &z, 1);
    if (status) {
        mutex->owner = thrd_current();
        atomic_store(&mutex->references, 1);
        return thrd_success;
    }
    return thrd_busy;
}

static int
__perform_lock(
    _In_ mtx_t* mutex,
    _In_ size_t timeout)
{
    FutexParameters_t parameters;
    int initialcount;
    int status;
    int z = 0;
    int i;
    
    // If this thread already holds the mutex,
    // increase ref count, but only if we're recursive 
    if (mutex->flags & mtx_recursive) {
        while (1) {
            initialcount = atomic_load(&mutex->references);
            if (initialcount != 0 && mutex->owner == thrd_current()) {
                status = atomic_compare_exchange_weak_explicit(&mutex->references, 
                    &initialcount, initialcount + 1, memory_order_release,
                    memory_order_acquire);
                if (status) {
                    return thrd_success;
                }
                continue;
            }
            break;
        }
    }
    
    parameters._futex0  = &mutex->value;
    parameters._val0    = 2; // we always sleep on expecting a two
    parameters._timeout = timeout;
    parameters._flags   = FUTEX_WAIT_PRIVATE;
    
    // On multicore systems the lock might be released rather quickly
    // so we perform a number of initial spins before going to sleep,
    // and only in the case that there are no sleepers && locked
    status = atomic_compare_exchange_strong(&mutex->value, &z, 1);
    if (!status) {
        if (SystemInfo.NumberOfActiveCores > 1 && z == 1) {
            for (i = 0; i < MUTEX_SPINS; i++) {
                if (mtx_trylock(mutex) == thrd_success) {
                    return thrd_success;
                }
            }
        }
        
        // Loop untill we get the lock
        if (z != 0) {
            if (z != 2) {
                z = atomic_exchange(&mutex->value, 2);
            }
            while (z != 0) {
                if (Syscall_FutexWait(&parameters) == OsTimeout) {
                    return thrd_timedout;
                }
                if (mutex->flags & MUTEX_DESTROYED) {
                    return thrd_error;
                }
                z = atomic_exchange(&mutex->value, 2);
            }
        }
    }

    mutex->owner = thrd_current();
    atomic_store(&mutex->references, 1);
    return thrd_success;
}

int
mtx_lock(
    _In_ mtx_t* mutex)
{
    if (mutex == NULL) {
        return thrd_error;
    }
    return __perform_lock(mutex, 0);
}

int
mtx_timedlock(
    _In_ mtx_t* restrict                 mutex,
    _In_ const struct timespec* restrict time_point)
{
    time_t          msec = 0;
	struct timespec now, result;
    
    if (mutex == NULL || !(mutex->flags & mtx_timed)) {
        return thrd_error;
    }
    
    // Calculate time to sleep
	timespec_get(&now, TIME_UTC);
    timespec_diff(time_point, &now, &result);
    msec = result.tv_sec * MSEC_PER_SEC;
    if (result.tv_nsec != 0) {
        msec += ((result.tv_nsec - 1) / NSEC_PER_MSEC) + 1;
    }
    return __perform_lock(mutex, msec);
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
    initialcount = atomic_load(&mutex->references);
    if (initialcount == 0 || mutex->owner != thrd_current()) {
        return thrd_error;
    }
    
    initialcount = atomic_fetch_sub(&mutex->references, 1);
    if ((initialcount - 1) == 0) {
        parameters._futex0  = &mutex->value;
        parameters._val0    = 1;
        parameters._flags   = FUTEX_WAKE_PRIVATE;

        mutex->owner = UUID_INVALID;
        
        initialcount = atomic_fetch_sub(&mutex->value, 1);
        if (initialcount != 1) {
            atomic_store(&mutex->value, 0);
            Syscall_FutexWake(&parameters);
        }
    }
    return thrd_success;
}
