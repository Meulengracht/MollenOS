/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
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
 * MollenOS MCore - Mutex Synchronization Primitve
 */

/* Includes 
 * - System */
#include <system/thread.h>
#include <scheduler.h>
#include <mutex.h>
#include <heap.h>

/* Includes
 * - Library */
#include <stddef.h>
#include <assert.h>

/* MutexCreate
 * Allocates a new mutex and initializes it to default values. */
Mutex_t*
MutexCreate(void)
{
    // Allocate a new instance
    Mutex_t* Mutex = (Mutex_t*)kmalloc(sizeof(Mutex_t));
    if (Mutex == NULL) {
        return NULL;
    }

    // Initialize it and return
	MutexConstruct(Mutex);
    Mutex->Cleanup = 1;
	return Mutex;
}

/* MutexConstruct
 * Initializes an already allocated mutex-resource. */
void
MutexConstruct(
    _In_ Mutex_t *Mutex)
{
    // Initialize members
	Mutex->Blocker = 0;
	Mutex->Blocks = 0;
    Mutex->Cleanup = 0;
}

/* MutexDestroy
 * Wakes up all sleepers on the mutex and frees resources. */
void
MutexDestroy(
    _In_ Mutex_t *Mutex)
{
	SchedulerThreadWakeAll((uintptr_t*)Mutex);
    if (Mutex->Cleanup == 1) {
        kfree(Mutex);
    }
}

/* MutexTryLock
 * Tries to acquire the mutex-lock within the time-out value. */
OsStatus_t
MutexTryLock(
    _In_ Mutex_t *Mutex,
    _In_ size_t Timeout)
{
	// Mutexes are inherintly re-entrant, so check.
	if (Mutex->Blocks != 0 
		&& Mutex->Blocker == ThreadingGetCurrentThreadId()) {
		Mutex->Blocks++;
		return OsSuccess;
	}

	// Try to acquire the mutex.
	while (Mutex->Blocks != 0) {
        if (SchedulerThreadSleep((uintptr_t*)Mutex, Timeout)
            == SCHEDULER_SLEEP_TIMEOUT) {
            return OsError;
        }
	}

	// We own the mutex now!
	Mutex->Blocks = 1;
	Mutex->Blocker = ThreadingGetCurrentThreadId();
	return OsSuccess;
}

/* MutexLock
 * Waits indefinitely for the mutex lock. */
OsStatus_t
MutexLock(
    _In_ Mutex_t *Mutex)
{
    // Run with infinite timouet
    return MutexTryLock(Mutex, 0);
}

/* MutexUnlock
 * Unlocks the mutex, by reducing lock-count by one. */
void
MutexUnlock(
    _In_ Mutex_t *Mutex)
{
    // Sanitize the state first.
    assert(Mutex != NULL);
    assert(Mutex->Blocks > 0);
    assert(Mutex->Blocker == ThreadingGetCurrentThreadId());

    // Reduce and wake people up
	Mutex->Blocks--;
	if (Mutex->Blocks == 0) {
		SchedulerThreadWake((uintptr_t*)Mutex);
    }
}
