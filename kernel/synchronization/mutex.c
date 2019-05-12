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
    
    SchedulerLockedQueueConstruct(&Mutex->Queue);
    SpinlockReset(&Mutex->SyncObject, Configuration);
    Mutex->Flags = Configuration;
}

OsStatus_t
MutexTryLock(
    _In_ Mutex_t* Mutex)
{
    assert(Mutex != NULL);
    return SpinlockTryAcquire(&Mutex->SyncObject);
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
            if (SpinlockTryAcquire(&Mutex->SyncObject) == OsSuccess) {
                return;
            }
        }
    }
    
    // Get lock loop, this tries to acquire the lock in an enternal loop
    dslock(&Mutex->Queue.SyncObject);
    while (1) {
        if (SpinlockTryAcquire(&Mutex->SyncObject) == OsSuccess) {
            break;
        }
        SchedulerBlock(&Mutex->Queue, 0);
    }
    dsunlock(&Mutex->Queue.SyncObject);
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
            if (SpinlockTryAcquire(&Mutex->SyncObject) == OsSuccess) {
                return OsSuccess;
            }
        }
    }
    
    // Get lock loop, this tries to acquire the lock in an enternal loop
    dslock(&Mutex->Queue.SyncObject);
    while (Status != OsTimeout) {
        if (SpinlockTryAcquire(&Mutex->SyncObject) == OsSuccess) {
            break;
        }
        Status = SchedulerBlock(&Mutex->Queue, Timeout);
    }
    dsunlock(&Mutex->Queue.SyncObject);
    return Status;
}

void
MutexUnlock(
    _In_ Mutex_t* Mutex)
{
    int References;

    assert(Mutex != NULL);

    // Is this the last reference?
    dslock(&Mutex->Queue.SyncObject);
    References = atomic_load(&Mutex->SyncObject.References);
    SpinlockRelease(&Mutex->SyncObject);
    if (References == 1) {
        SchedulerUnblock(&Mutex->Queue);
    }
    dsunlock(&Mutex->Queue.SyncObject);
}
