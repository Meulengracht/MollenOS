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
#define __MODULE "MUTX"

#include <assert.h>
#include <ddk/barrier.h>
#include <debug.h>
#include <futex.h>
#include <limits.h>
#include <machine.h>
#include <mutex.h>
#include <scheduler.h>

#define MUTEX_SPINS 1000

void
MutexConstruct(
    _In_ Mutex_t* Mutex,
    _In_ Flags_t  Configuration)
{
    assert(Mutex != NULL);
    
    Mutex->Owner      = UUID_INVALID;
    Mutex->Flags      = Configuration;
    Mutex->References = ATOMIC_VAR_INIT(0);
    Mutex->Value      = ATOMIC_VAR_INIT(0);
    smp_wmb();
}

OsStatus_t
MutexDestruct(
    _In_ Mutex_t* Mutex)
{
    if (!Mutex) {
        return OsInvalidParameters;
    }
    return FutexWake(&Mutex->Value, INT_MAX, FUTEX_WAKE_PRIVATE);
}

OsStatus_t
MutexTryLock(
    _In_ Mutex_t* Mutex)
{
    int Status;
    int Zero = 0;
    int Count;
    
    assert(Mutex != NULL);
    
    // If this thread already holds the mutex,
    // increase ref count, but only if we're recursive
    if (Mutex->Flags & MUTEX_RECURSIVE) {
        while (1) {
            Count = atomic_load(&Mutex->References);
            if (Count != 0 && Mutex->Owner == GetCurrentThreadId()) {
                Status = atomic_compare_exchange_weak_explicit(&Mutex->References, 
                    &Count, Count + 1, memory_order_release,
                    memory_order_acquire);
                if (Status) {
                    return OsSuccess;
                }
                continue;
            }
            break;
        }
    }
    
    Status = atomic_compare_exchange_strong(&Mutex->Value, &Zero, 1);
    if (Status) {
        Mutex->Owner = GetCurrentThreadId();
        atomic_store(&Mutex->References, 1);
        return OsSuccess;
    }
    return OsError;
}

static OsStatus_t
__MutexPerformLock(
    _In_ Mutex_t* Mutex,
    _In_ size_t   Timeout)
{
    int Status;
    int Zero = 0;
    int Count;
    int i;
    
    // If this thread already holds the mutex,
    // increase ref count, but only if we're recursive 
    if (Mutex->Flags & MUTEX_RECURSIVE) {
        while (1) {
            Count = atomic_load(&Mutex->References);
            if (Count != 0 && Mutex->Owner == GetCurrentThreadId()) {
                Status = atomic_compare_exchange_weak_explicit(&Mutex->References, 
                    &Count, Count + 1, memory_order_release,
                    memory_order_acquire);
                if (Status) {
                    return OsSuccess;
                }
                continue;
            }
            break;
        }
    }
    
    // On multicore systems the lock might be released rather quickly
    // so we perform a number of initial spins before going to sleep,
    // and only in the case that there are no sleepers && locked
    Status = atomic_compare_exchange_strong(&Mutex->Value, &Zero, 1);
    if (!Status) {
        if (atomic_load(&GetMachine()->NumberOfActiveCores) > 1) {
            for (i = 0; i < MUTEX_SPINS; i++) {
                if (MutexTryLock(Mutex) == OsSuccess) {
                    return OsSuccess;
                }
            }
        }
        
        // Loop untill we get the lock
        if (Zero != 0) {
            if (Zero != 2) {
                Zero = atomic_exchange(&Mutex->Value, 2);
            }
            while (Zero != 0) {
                if (FutexWait(&Mutex->Value, 2, FUTEX_WAIT_PRIVATE, Timeout) == OsTimeout) {
                    return OsTimeout;
                }
                Zero = atomic_exchange(&Mutex->Value, 2);
            }
        }
    }
    
    Mutex->Owner = GetCurrentThreadId();
    atomic_store(&Mutex->References, 1);
    return OsSuccess;
}

void
MutexLock(
    _In_ Mutex_t* Mutex)
{
    assert(Mutex != NULL);
    (void)__MutexPerformLock(Mutex, 0);
}

OsStatus_t
MutexLockTimed(
    _In_ Mutex_t* Mutex,
    _In_ size_t   Timeout)
{
    if (!Mutex) {
        return OsInvalidParameters;
    }
    
    if (!(Mutex->Flags & MUTEX_TIMED)) {
        FATAL(FATAL_SCOPE_KERNEL, "Tried to use LockTimed on a non timed mutex");
        return OsError;
    }
    return __MutexPerformLock(Mutex, Timeout);
}

void
MutexUnlock(
    _In_ Mutex_t* Mutex)
{
    int Count;
    assert(Mutex != NULL);

    // Sanitize state of the mutex, are we even able to unlock it?
    Count = atomic_load(&Mutex->References);
    assert(Count != 0);
    assert(Mutex->Owner == GetCurrentThreadId());
    
    Count = atomic_fetch_sub(&Mutex->References, 1);
    if ((Count - 1) == 0) {
        Mutex->Owner = UUID_INVALID;
        
        Count = atomic_fetch_sub(&Mutex->Value, 1);
        if (Count != 1) {
            atomic_store(&Mutex->Value, 0);
            FutexWake(&Mutex->Value, 1, FUTEX_WAKE_PRIVATE);
        }
    }
}
