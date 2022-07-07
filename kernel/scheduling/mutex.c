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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Synchronization (Mutex)
 * - Hybrid mutex implementation. Contains a spinlock that serves
 *   as the locking primitive, with extended block capabilities.
 */
#define __MODULE "MUTX"

#include <arch/interrupts.h>
#include <assert.h>
#include <handle.h>
#include <machine.h>
#include <mutex.h>
#include <scheduler.h>
#include <arch/thread.h>

// Internal, very private definitions
#define MUTEX_FLAG_INVALID 0x1000
#define MUTEX_FLAG_PENDING 0x2000

#define MUTEX_SPINS 100

static inline int __HasFlags(Mutex_t* mutex, unsigned int flags)
{
    return (atomic_load(&mutex->flags) & flags) == flags;
}

static inline void __ClearFlags(Mutex_t* mutex, unsigned int flags)
{
    (void)atomic_fetch_xor(&mutex->flags, flags);
}

static inline void __SetFlags(Mutex_t* mutex, unsigned int flags)
{
    (void)atomic_fetch_or(&mutex->flags, flags);
}

// Try performing a quick lock of the mutex by using cmpxchg
static oscode_t
__TryQuickLock(
        _In_  Mutex_t* mutex,
        _Out_ uuid_t*  ownerOut)
{
    uuid_t currentThread = ThreadCurrentHandle();
    uuid_t free          = UUID_INVALID;
    int    status        = atomic_compare_exchange_strong(&mutex->owner, &free, currentThread);
    if (!status) {
        if (free == currentThread) {
            assert(__HasFlags(mutex, MUTEX_FLAG_RECURSIVE));
            mutex->referenceCount++;
            return OsOK;
        }
        *ownerOut = free;
        return OsBusy;
    }

    mutex->referenceCount = 1;
    return OsOK;
}

// On multicore systems the lock might be released rather quickly
// so we perform a number of initial spins before going to sleep,
// and only in the case that there are no sleepers && locked
static oscode_t __TrySpinOnOwner(Mutex_t* mutex, uuid_t owner)
{
    uuid_t currentOwner = owner;

    if (atomic_load(&GetMachine()->NumberOfActiveCores) > 1) {
        // they must be on seperate cores, otherwise it makes no sense
        SchedulerObject_t* ownerSchedulerObject = ThreadSchedulerHandle(THREAD_GET(owner));
        if (CpuCoreId(CpuCoreCurrent()) != SchedulerObjectGetAffinity(ownerSchedulerObject)) {
            for (int i = 0; i < MUTEX_SPINS && currentOwner == owner; i++) {
                if (__TryQuickLock(mutex, &currentOwner) == OsOK) {
                    return OsOK;
                }
            }
        }
    }
    return OsError;
}

static oscode_t __SlowLock(Mutex_t* mutex, size_t timeout)
{
    irqstate_t intStatus;
    uuid_t      owner;

    // Disable interrupts and try to acquire the lock or wait for the lock
    // to unlock if its held on another CPU - however we only wait for a brief period
    intStatus = InterruptDisable();
    if (__TryQuickLock(mutex, &owner) == OsOK ||
        __TrySpinOnOwner(mutex, owner) == OsOK) {
        InterruptRestoreState(intStatus);
        return OsOK;
    }

    // After we acquire the lock we want to make sure that we still cant
    // get the lock before adding us to the queue
    spinlock_acquire(&mutex->syncObject);
    if (__TryQuickLock(mutex, &owner) == OsOK) {
        goto exit;
    }

    // mark us having waiters
    __SetFlags(mutex, MUTEX_FLAG_PENDING);
    while (1) {
        // block task and then reenable interrupts
        SchedulerBlock(&mutex->blockQueue, timeout * NSEC_PER_MSEC);
        spinlock_release(&mutex->syncObject);
        InterruptRestoreState(intStatus);
        ArchThreadYield();

        // at this point we've been waken up either by an unlock or timeout
        if (SchedulerGetTimeoutReason() == OsTimeout) {
            return OsTimeout;
        }

        // was the mutex destroyed
        if (__HasFlags(mutex, MUTEX_FLAG_INVALID)) {
            return OsInvalidParameters;
        }

        // try acquiring the mutex
        intStatus = InterruptDisable();
        spinlock_acquire(&mutex->syncObject);
        if (__TryQuickLock(mutex, &owner) == OsOK) {
            break;
        }
    }

exit:
    spinlock_release(&mutex->syncObject);
    InterruptRestoreState(intStatus);
    return OsOK;
}

void
MutexConstruct(
    _In_ Mutex_t*     mutex,
    _In_ unsigned int configuration)
{
    assert(mutex != NULL);

    list_construct(&mutex->blockQueue);
    spinlock_init(&mutex->syncObject, spinlock_plain);
    mutex->owner          = ATOMIC_VAR_INIT(UUID_INVALID);
    mutex->flags          = ATOMIC_VAR_INIT(configuration);
    mutex->referenceCount = 0;
}

void
MutexDestruct(
    _In_ Mutex_t* mutex)
{
    element_t* waiter;

    assert(mutex != NULL);

    // first mark the mutex invalid
    mutex->referenceCount = 0;
    __SetFlags(mutex, MUTEX_FLAG_INVALID);
    __ClearFlags(mutex, MUTEX_FLAG_PENDING);
    atomic_store(&mutex->owner, UUID_INVALID);

    spinlock_acquire(&mutex->syncObject);
    waiter = list_front(&mutex->blockQueue);
    while (waiter) {
        list_remove(&mutex->blockQueue, waiter);
        (void)SchedulerQueueObject(waiter->value);
        waiter = list_front(&mutex->blockQueue);
    }
    spinlock_release(&mutex->syncObject);
}

oscode_t
MutexTryLock(
    _In_ Mutex_t* mutex)
{
    uuid_t owner;

    if (!mutex) {
        return OsInvalidParameters;
    }
    return __TryQuickLock(mutex, &owner);
}

void
MutexLock(
    _In_ Mutex_t* mutex)
{
    if (!mutex) {
        return;
    }
    (void)__SlowLock(mutex, 0);
}

oscode_t
MutexLockTimed(
    _In_ Mutex_t* mutex,
    _In_ size_t   timeout)
{
    if (!mutex || !__HasFlags(mutex, MUTEX_FLAG_TIMED)) {
        return OsInvalidParameters;
    }
    return __SlowLock(mutex, timeout);
}

void
MutexUnlock(
    _In_ Mutex_t* mutex)
{
    element_t* waiter;
    uuid_t     owner;

    if (!mutex) {
        return;
    }

    // protect against bad free
    owner = atomic_load(&mutex->owner);
    if (owner != ThreadCurrentHandle()) {
        return;
    }

    // protect against recursion
    mutex->referenceCount--;
    if (mutex->referenceCount) {
        return;
    }

    // free the lock immediately
    atomic_store(&mutex->owner, UUID_INVALID);
    if (!__HasFlags(mutex, MUTEX_FLAG_PENDING))
        return;

    spinlock_acquire(&mutex->syncObject);
    waiter = list_front(&mutex->blockQueue);
    if (waiter) {
        list_remove(&mutex->blockQueue, waiter);
        SchedulerQueueObject(waiter->value);

        if (!list_front(&mutex->blockQueue)) {
            __ClearFlags(mutex, MUTEX_FLAG_PENDING);
        }
    }
    spinlock_release(&mutex->syncObject);
}
