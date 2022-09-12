/**
 * MollenOS
 *
 * Copyright 2022, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <ddk/barrier.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/mollenos.h>
#include <os/futex.h>
#include <os/mutex.h>
#include <os/threads.h>
#include <time.h>

#define MUTEX_SPINS     1000
#define MUTEX_DESTROYED 0x1000

static SystemDescriptor_t SystemInfo = { 0 };

oserr_t
MutexInitialize(
        _In_ Mutex_t* mutex,
        _In_ int      flags)
{
    if (mutex == NULL) {
        return OsInvalidParameters;
    }

    // Get information about the system
    if (SystemInfo.NumberOfActiveCores == 0) {
        SystemQuery(&SystemInfo);
    }
    
    mutex->Flags = flags;
    mutex->Owner = UUID_INVALID;
    mutex->Value = ATOMIC_VAR_INIT(0);
    mutex->References = ATOMIC_VAR_INIT(0);
    return OsOK;
}

void
MutexDestroy(
        _In_ Mutex_t* mutex)
{
    FutexParameters_t parameters;
    
    if (mutex != NULL) {
        mutex->Flags |= MUTEX_DESTROYED;
        smp_wmb();
        
        parameters._futex0 = &mutex->Value;
        parameters._val0   = INT_MAX;
        parameters._flags  = FUTEX_WAKE_PRIVATE;
        Syscall_FutexWake(&parameters);
    }
}

oserr_t
MutexTryLock(
        _In_ Mutex_t* mutex)
{
    int initialcount;
    int z = 0;
    int status;
    
    if (mutex == NULL) {
        return OsInvalidParameters;
    }
    
    // If this thread already holds the mutex,
    // increase ref count, but only if we're recursive
    if (mutex->Flags & MUTEX_RECURSIVE) {
        while (1) {
            initialcount = atomic_load(&mutex->References);
            if (initialcount != 0 && mutex->Owner == ThreadsCurrentId()) {
                status = atomic_compare_exchange_weak_explicit(&mutex->References,
                    &initialcount, initialcount + 1, memory_order_release,
                    memory_order_acquire);
                if (status) {
                    return OsOK;
                }
                continue;
            }
            break;
        }
    }
    
    status = atomic_compare_exchange_strong(&mutex->Value, &z, 1);
    if (status) {
        mutex->Owner = ThreadsCurrentId();
        atomic_store(&mutex->References, 1);
        return OsOK;
    }
    return OsBusy;
}

static int
__perform_lock(
    _In_ Mutex_t* mutex,
    _In_ size_t   timeout)
{
    FutexParameters_t parameters;
    int initialcount;
    int status;
    int z = 0;
    int i;
    
    // If this thread already holds the mutex,
    // increase ref count, but only if we're recursive 
    if (mutex->Flags & MUTEX_RECURSIVE) {
        while (1) {
            initialcount = atomic_load(&mutex->References);
            if (initialcount != 0 && mutex->Owner == ThreadsCurrentId()) {
                status = atomic_compare_exchange_weak_explicit(&mutex->References,
                    &initialcount, initialcount + 1, memory_order_release,
                    memory_order_acquire);
                if (status) {
                    return OsOK;
                }
                continue;
            }
            break;
        }
    }
    
    parameters._futex0  = &mutex->Value;
    parameters._val0    = 2; // we always sleep on expecting a two
    parameters._timeout = timeout;
    parameters._flags   = FUTEX_WAIT_PRIVATE;
    
    // On multicore systems the lock might be released rather quickly
    // so we perform a number of initial spins before going to sleep,
    // and only in the case that there are no sleepers && locked
    status = atomic_compare_exchange_strong(&mutex->Value, &z, 1);
    if (!status) {
        if (SystemInfo.NumberOfActiveCores > 1 && z == 1) {
            for (i = 0; i < MUTEX_SPINS; i++) {
                if (MutexTryLock(mutex) == OsOK) {
                    return OsOK;
                }
            }
        }
        
        // Loop untill we get the lock
        if (z != 0) {
            if (z != 2) {
                z = atomic_exchange(&mutex->Value, 2);
            }
            while (z != 0) {
                if (Syscall_FutexWait(&parameters) == OsTimeout) {
                    return OsTimeout;
                }
                if (mutex->Flags & MUTEX_DESTROYED) {
                    return OsCancelled;
                }
                z = atomic_exchange(&mutex->Value, 2);
            }
        }
    }

    mutex->Owner = ThreadsCurrentId();
    atomic_store(&mutex->References, 1);
    return OsOK;
}

oserr_t
MutexLock(
        _In_ Mutex_t* mutex)
{
    if (mutex == NULL) {
        return OsInvalidParameters;
    }
    return __perform_lock(mutex, 0);
}

oserr_t
MutexTimedLock(
        _In_ Mutex_t* restrict               mutex,
        _In_ const struct timespec *restrict timePoint)
{
    time_t          msec = 0;
	struct timespec now, result;
    
    if (mutex == NULL || !(mutex->Flags & MUTEX_TIMED)) {
        return OsInvalidParameters;
    }
    
    // Calculate time to sleep
	timespec_get(&now, TIME_UTC);
    timespec_diff(&now, timePoint, &result);
    if (result.tv_sec < 0) {
        return OsTimeout;
    }

    msec = result.tv_sec * MSEC_PER_SEC;
    if (result.tv_nsec != 0) {
        msec += ((result.tv_nsec - 1) / NSEC_PER_MSEC) + 1;
    }
    return __perform_lock(mutex, msec);
}

oserr_t
MutexUnlock(
        _In_ Mutex_t* mutex)
{
    FutexParameters_t parameters;
    int               initialcount;
    if (mutex == NULL) {
        return OsInvalidParameters;
    }

    // Sanitize state of the mutex, are we even able to unlock it?
    initialcount = atomic_load(&mutex->References);
    if (initialcount == 0 || mutex->Owner != ThreadsCurrentId()) {
        return OsInvalidPermissions;
    }
    
    initialcount = atomic_fetch_sub(&mutex->References, 1);
    if ((initialcount - 1) == 0) {
        parameters._futex0  = &mutex->Value;
        parameters._val0    = 1;
        parameters._flags   = FUTEX_WAKE_PRIVATE;

        mutex->Owner = UUID_INVALID;
        
        initialcount = atomic_fetch_sub(&mutex->Value, 1);
        if (initialcount != 1) {
            atomic_store(&mutex->Value, 0);
            Syscall_FutexWake(&parameters);
        }
    }
    return OsOK;
}
