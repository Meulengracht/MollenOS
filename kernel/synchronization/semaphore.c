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
 * Synchronization
 * - Counting semaphores implementation, using safe passages known as
 *   atomic sections in the operating system to synchronize in a kernel env
 */
#define __MODULE "SEM1"
//#define __TRACE

#include <arch/thread.h>
#include <arch/utils.h>
#include <semaphore.h>
#include <scheduler.h>
#include <debug.h>
#include <stddef.h>
#include <assert.h>

void
SemaphoreConstruct(
    _In_ Semaphore_t* Semaphore,
    _In_ int          InitialValue,
    _In_ int          MaximumValue)
{
	assert(Semaphore != NULL);
	assert(InitialValue >= 0);
    assert(MaximumValue >= InitialValue);

    CollectionConstruct(&Semaphore->WaitQueue, KeyId);
    spinlock_init(&Semaphore->SyncObject, spinlock_plain);
    Semaphore->MaxValue = MaximumValue;
	Semaphore->Value    = ATOMIC_VAR_INIT(InitialValue);
}

OsStatus_t
SemaphoreWaitSimple(
    _In_ Semaphore_t* Semaphore,
    _In_ size_t       Timeout)
{
    OsStatus_t Status;
    int        Value;
    assert(Semaphore != NULL);
    
    spinlock_acquire(&Semaphore->SyncObject);
    Value = atomic_fetch_sub(&Semaphore->Value, 1);
    if (Value <= 0) {
        Status = SchedulerBlock(&Semaphore->WaitQueue, &Semaphore->SyncObject, Timeout);
        if (Status == OsTimeout) {
            // add one to value to account for the loss of value
            atomic_fetch_add(&Semaphore->Value, 1);
        }
        spinlock_release(&Semaphore->SyncObject);
    }
    else {
        spinlock_release(&Semaphore->SyncObject);
        Status = OsSuccess;
    }
    return Status;
}

OsStatus_t
SemaphoreWait(
    _In_ Semaphore_t* Semaphore,
    _In_ Mutex_t*     Mutex,
    _In_ size_t       Timeout)
{
    OsStatus_t Status;
    int        Value;
    assert(Semaphore != NULL);
    assert(Mutex != NULL);
    
    spinlock_acquire(&Semaphore->SyncObject);
    Value = atomic_fetch_sub(&Semaphore->Value, 1);
    if (Value <= 0) {
        MutexUnlock(Mutex);
        Status = SchedulerBlock(&Semaphore->WaitQueue, &Semaphore->SyncObject, Timeout);
        if (Status == OsTimeout) {
            // add one to value to account for the loss of value
            atomic_fetch_add(&Semaphore->Value, 1);
        }
        spinlock_release(&Semaphore->SyncObject);
        MutexLock(Mutex);
    }
    else {
        spinlock_release(&Semaphore->SyncObject);
        Status = OsSuccess;
    }
    return Status;
}

void
SemaphoreSignal(
    _In_ Semaphore_t* Semaphore,
    _In_ int          Value)
{
    int CurrentValue;
    int i;
    assert(Semaphore != NULL);

    spinlock_acquire(&Semaphore->SyncObject);
    CurrentValue = atomic_load(&Semaphore->Value);
    if (CurrentValue < Semaphore->MaxValue) {
        for (i = 0; (i < Value) && (CurrentValue + i) < Semaphore->MaxValue; i++) {
            if ((CurrentValue + i) < 0) {
                SchedulerUnblock(&Semaphore->WaitQueue);
            }
            atomic_fetch_add(&Semaphore->Value, 1);
        }
    }
    spinlock_release(&Semaphore->SyncObject);
}
