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

#include <assert.h>
#include <machine.h>
#include <mutex.h>
#include <scheduler.h>

#define MUTEX_SPINS 1000

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
    int Value;
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

    // Otherwise get initial value, check it, go to sleep.. In a loop
    while (1) {
        Value = 0;//atomic_exchange(&Mutex->SyncObject.Value, 1);
        if (!Value) {
            return; // lock acquired
        }
        SchedulerAtomicThreadSleep(&Mutex->SyncObject.Value, &Value, 0);
    }
}

void
MutexUnlock(
    _In_ Mutex_t* Mutex)
{
    int References;

    assert(Mutex != NULL);

    // Is this the last reference?
    References = atomic_load(&Mutex->SyncObject.References);
    SpinlockRelease(&Mutex->SyncObject);
    if (References == 1) {
        // Wake up a thread
        
    }
}
