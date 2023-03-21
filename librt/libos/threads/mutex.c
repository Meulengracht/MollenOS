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

#include <internal/_syscalls.h>
#include <os/mollenos.h>
#include <os/futex.h>
#include <os/mutex.h>
#include <os/threads.h>
#include <limits.h>
#include <time.h>

#define MUTEX_SPINS     1000
#define MUTEX_DESTROYED 0x1000

static OSSystemCPUInfo_t g_cpuInfo = { 0 };

// Mutex::State is made up of 24 bits of ownership, which corresponds to the thread
// id. We don't actually need the full 24 bits, but it makes things easier. This way
// we allow up to 256 recursive acquires, and should this be reached we abort the program
// due to toxic behavior.
#define __BUILD_STATE(owner, refcount) (((owner) << 8) | ((refcount) & 0xFF))
#define __STATE_OWNER(state)           (((state) & 0xFFFFFF00) >> 8)
#define __STATE_REFCOUNT(state)        ((state) & 0xFF)

oserr_t
MutexInitialize(
        _In_ Mutex_t* mutex,
        _In_ int      flags)
{
    if (mutex == NULL) {
        return OS_EINVALPARAMS;
    }

    // Get information about the system
    if (g_cpuInfo.NumberOfActiveCores == 0) {
        size_t bytesQueried;
        OSSystemQuery(
                OSSYSTEMQUERY_CPUINFO,
                &g_cpuInfo,
                sizeof(OSSystemCPUInfo_t),
                &bytesQueried
        );
    }
    
    mutex->Flags = flags;
    mutex->Value = 0;
    mutex->State = __BUILD_STATE(UUID_INVALID, 0);
    return OS_EOK;
}

void
MutexDestroy(
        _In_ Mutex_t* mutex)
{
    OSFutexParameters_t parameters;
    
    if (mutex != NULL) {
        mutex->Flags |= MUTEX_DESTROYED;

        parameters.Futex0    = &mutex->Value;
        parameters.Expected0 = INT_MAX;
        parameters.Flags     = FUTEX_FLAG_WAKE | FUTEX_FLAG_PRIVATE;
        OSFutex(&parameters, NULL);
    }
}

static oserr_t
__TryLockRecursive(
        _In_ Mutex_t* mutex)
{
    while (1) {
        unsigned int state    = atomic_load(&mutex->State);
        uuid_t       owner    = __STATE_OWNER(state);
        unsigned int refcount = __STATE_REFCOUNT(state);
        if (refcount != 0 && owner == ThreadsCurrentId()) {
            unsigned int newState = __BUILD_STATE(owner, refcount + 1);
            int status = atomic_compare_exchange_weak(&mutex->State, &state, newState);
            if (status) {
                return OS_EOK;
            }
            continue;
        }
        break;
    }
    return OS_EBUSY;
}

oserr_t
MutexTryLock(
        _In_ Mutex_t* mutex)
{
    int z = 0;
    int status;
    
    if (mutex == NULL) {
        return OS_EINVALPARAMS;
    }

    // If the mutex is recursive, we add one refcount to the state. We do not
    // need to check this in any memory-safe way, as Mutex::Flags does not change
    // after creation.
    if (mutex->Flags & MUTEX_RECURSIVE) {
        if (__TryLockRecursive(mutex) == OS_EOK) {
            return OS_EOK;
        }
    }
    
    status = atomic_compare_exchange_strong(&mutex->Value, &z, 1);
    if (!status) {
        return OS_EBUSY;
    }

    atomic_store(&mutex->State, __BUILD_STATE(ThreadsCurrentId(), 1));
    return OS_EOK;
}

static int
__perform_lock(
        _In_ Mutex_t* restrict               mutex,
        _In_ const struct timespec *restrict timePoint)
{
    OSFutexParameters_t parameters;
    int                 status;
    int               z = 0;
    int               i;

    // If the mutex is recursive, we add one refcount to the state. We do not
    // need to check this in any memory-safe way, as Mutex::Flags does not change
    // after creation.
    if (mutex->Flags & MUTEX_RECURSIVE) {
        if (__TryLockRecursive(mutex) == OS_EOK) {
            return OS_EOK;
        }
    }
    
    parameters.Futex0    = &mutex->Value;
    parameters.Expected0 = 2; // we always sleep on expecting a two
    parameters.Deadline  = timePoint == NULL ? NULL : &(OSTimestamp_t) {
        .Seconds = timePoint->tv_sec,
        .Nanoseconds = timePoint->tv_nsec
    };
    parameters.Flags     = FUTEX_FLAG_WAIT | FUTEX_FLAG_PRIVATE;
    
    // On multicore systems the lock might be released rather quickly,
    // so we perform a number of initial spins before going to sleep,
    // and only in the case that there are no sleepers && locked
    status = atomic_compare_exchange_strong(&mutex->Value, &z, 1);
    if (!status) {
        if (g_cpuInfo.NumberOfActiveCores > 1 && z == 1) {
            for (i = 0; i < MUTEX_SPINS; i++) {
                if (MutexTryLock(mutex) == OS_EOK) {
                    return OS_EOK;
                }
            }
        }
        
        // Loop untill we get the lock. How this works is that we use the
        // value 0 for unlocked, 1 for locked, and 2 for locked, locker pending.
        // This allows us to improve for effeciency when unlocking. If the unlocked
        // value was 2, then we wake up.
        if (z != 0) {
            // Mark the value as pending before continuing
            if (z != 2) {
                // Get the previous value to make sure it wasn't unlocked this nanosecond.
                z = atomic_exchange(&mutex->Value, 2);
            }

            while (z != 0) {
                if (OSFutex(&parameters, NULL) == OS_ETIMEOUT) {
                    return OS_ETIMEOUT;
                }

                // Check that the mutex wasn't flagged for destruction, no need
                // wait for a mutex that won't ever be signalled. Probably needs
                // a barrier before reading this value
                if (mutex->Flags & MUTEX_DESTROYED) {
                    return OS_ECANCELLED;
                }
                z = atomic_exchange(&mutex->Value, 2);
            }
        }
    }

    atomic_store(&mutex->State, __BUILD_STATE(ThreadsCurrentId(), 1));
    return OS_EOK;
}

oserr_t
MutexLock(
        _In_ Mutex_t* mutex)
{
    if (mutex == NULL) {
        return OS_EINVALPARAMS;
    }
    return __perform_lock(mutex, NULL);
}

oserr_t
MutexTimedLock(
        _In_ Mutex_t* restrict               mutex,
        _In_ const struct timespec *restrict timePoint)
{
    if (mutex == NULL || !(mutex->Flags & MUTEX_TIMED)) {
        return OS_EINVALPARAMS;
    }

    return __perform_lock(mutex, timePoint);
}

oserr_t
MutexUnlock(
        _In_ Mutex_t* mutex)
{
    OSFutexParameters_t parameters;
    unsigned int        state, newState;
    uuid_t              owner;
    unsigned int        refcount;
    int                 status;
    int                 lockState;
    if (mutex == NULL) {
        return OS_EINVALPARAMS;
    }

    // Sanitize state of the mutex, are we even able to unlock it?
    state = atomic_load(&mutex->State);
    while (1) {
        owner = __STATE_OWNER(state);
        refcount = __STATE_REFCOUNT(state);
        if (refcount == 0 || owner != ThreadsCurrentId()) {
            return OS_EPERMISSIONS;
        }

        // If the refcount is 1, then it's our responsiblity to unlock it.
        if (refcount == 1) {
            newState = __BUILD_STATE(UUID_INVALID, 0);
            status = atomic_compare_exchange_strong(&mutex->State, &state, newState);
            if (!status) {
                // Uhhh something changed, try again
                continue;
            }

            break;
        } else {
            newState = __BUILD_STATE(owner, refcount - 1);
            status = atomic_compare_exchange_strong(&mutex->State, &state, newState);
            if (!status) {
                // Uhhh something changed, try again
                continue;
            }

            // And we're done, we don't need to wake anyone since we were
            // simply reducing the refcount.
            return OS_EOK;
        }
    }

    // get current lock state and act accordinly
    lockState = atomic_exchange(&mutex->Value, 0);
    if (lockState == 2) {
        parameters.Futex0    = &mutex->Value;
        parameters.Expected0 = 1;
        parameters.Flags     = FUTEX_FLAG_WAKE | FUTEX_FLAG_PRIVATE;
        OSFutex(&parameters, NULL);
    }
    return OS_EOK;
}
