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
 * Synchronization (Mutex)
 * - Hybrid mutex implementation. Contains a spinlock that serves
 *   as the locking primitive, with extended block capabilities.
 */
#define __MODULE "MUTX"

#include <assert.h>
#include <debug.h>
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
    
    // Initialize the lock object as requested per configuration
    // but the syncobject that protects the queue must be plain
    spinlock_init(&Mutex->LockObject, Configuration);
    spinlock_init(&Mutex->SyncObject, spinlock_plain);
    CollectionConstruct(&Mutex->WaitQueue, KeyId);
    Mutex->Flags = Configuration;
}

OsStatus_t
MutexTryLock(
    _In_ Mutex_t* Mutex)
{
    assert(Mutex != NULL);
    return spinlock_try_acquire(&Mutex->LockObject);
}

void
MutexLock(
    _In_ Mutex_t* Mutex)
{
    int i;

    assert(Mutex != NULL);

    // In a multcore environment the lock may not be held for long, so
    // perform X iterations before going for a longer block
    // period.
    if (GetMachine()->NumberOfActiveCores > 1) {
        for (i = 0; i < MUTEX_SPINS; i++) {
            if (spinlock_try_acquire(&Mutex->LockObject) == OsSuccess) {
                return;
            }
        }
    }
    
    // Get lock loop, this tries to acquire the lock in an enternal loop
    spinlock_acquire(&Mutex->SyncObject);
    while (1) {
        if (spinlock_try_acquire(&Mutex->LockObject) == OsSuccess) {
            break;
        }
        SchedulerBlock(&Mutex->WaitQueue, &Mutex->SyncObject, 0);
    }
    spinlock_release(&Mutex->SyncObject);
}

OsStatus_t
MutexLockTimed(
    _In_ Mutex_t* Mutex,
    _In_ size_t   Timeout)
{
    OsStatus_t Status = OsSuccess;
    int        i;

    assert(Mutex != NULL);
    if (!(Mutex->Flags & MUTEX_TIMED)) {
        FATAL(FATAL_SCOPE_KERNEL, "Tried to use LockTimed on a non timed mutex");
        return OsError;
    }
    
    // Use the regular lock if timeout is 0
    if (Timeout == 0) {
        MutexLock(Mutex);
        return OsSuccess;
    }

    // In a multcore environment the lock may not be held for long, so
    // perform X iterations before going for a longer block
    // period.
    if (GetMachine()->NumberOfActiveCores > 1) {
        for (i = 0; i < MUTEX_SPINS; i++) {
            if (spinlock_try_acquire(&Mutex->LockObject) == OsSuccess) {
                return OsSuccess;
            }
        }
    }
    
    // Get lock loop, this tries to acquire the lock in an enternal loop
    spinlock_acquire(&Mutex->SyncObject);
    while (Status != OsTimeout) {
        if (spinlock_try_acquire(&Mutex->LockObject) == OsSuccess) {
            break;
        }
        Status = SchedulerBlock(&Mutex->WaitQueue, &Mutex->SyncObject, Timeout);
    }
    spinlock_release(&Mutex->SyncObject);
    return Status;
}

void
MutexUnlock(
    _In_ Mutex_t* Mutex)
{
    assert(Mutex != NULL);

    // Is this the last reference?
    spinlock_acquire(&Mutex->SyncObject);
    if (spinlock_release(&Mutex->LockObject) == spinlock_released) {
        SchedulerUnblock(&Mutex->WaitQueue);
    }
    spinlock_release(&Mutex->SyncObject);
}
