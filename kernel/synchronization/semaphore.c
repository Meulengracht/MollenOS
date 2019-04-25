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
 * MollenOS Synchronization
 * - Counting semaphores implementation, using safe passages known as
 *   atomic sections in the operating system to synchronize in a kernel env
 */
#define __MODULE "SEM1"
//#define __TRACE
#ifdef __TRACE
#define __STRICT_ASSERT(x) assert(x)
#else
#define __STRICT_ASSERT(x) 
#endif

#include <arch/thread.h>
#include <arch/utils.h>
#include <semaphore_slim.h>
#include <scheduler.h>
#include <debug.h>

#include <stddef.h>
#include <assert.h>

/* SlimSemaphoreDestroy
 * Cleans up the semaphore, waking up all sleeper threads
 * to not have dead threads. */
void
SlimSemaphoreDestroy(
    _In_ SlimSemaphore_t*   Semaphore)
{
    // Wakeup threads
	SchedulerHandleSignalAll((uintptr_t*)&Semaphore->Value);
}

/* SlimSemaphoreConstruct
 * Constructs an already allocated semaphore and resets
 * it's value to the given initial value */
void
SlimSemaphoreConstruct(
    _In_ SlimSemaphore_t*   Semaphore,
    _In_ int                InitialValue,
    _In_ int                MaximumValue)
{
	// Sanitize values
	assert(Semaphore != NULL);
	assert(InitialValue >= 0);
    assert(MaximumValue >= InitialValue);

	// Initiate members
    Semaphore->MaxValue = MaximumValue;
	Semaphore->Value    = ATOMIC_VAR_INIT(InitialValue);
}

/* SlimSemaphoreWait
 * Waits for the semaphore signal with the optional time-out.
 * Returns SCHEDULER_SLEEP_OK or SCHEDULER_SLEEP_TIMEOUT */
int
SlimSemaphoreWait(
    _In_ SlimSemaphore_t*   Semaphore,
    _In_ size_t             Timeout)
{
    // Variables
    int Value = atomic_fetch_sub(&Semaphore->Value, 1) - 1;
    int Status;

    while (1) {
        if (Value >= 0) {
            Status = SCHEDULER_SLEEP_OK;
            break;
        }

        // Go to sleep atomically, check return value, if there was sync
        // issues try again
        Status = SchedulerAtomicThreadSleep(&Semaphore->Value, &Value, Timeout);
        if (Status != SCHEDULER_SLEEP_SYNC_FAILED) {
            break;
        }
    }
    return Status;
}

/* SlimSemaphoreSignal
 * Signals the semaphore with the given value, default is 1 */
OsStatus_t
SlimSemaphoreSignal(
    _In_ SlimSemaphore_t*   Semaphore,
    _In_ int                Value)
{
	// Variables
    OsStatus_t Status = OsError;
    int CurrentValue;
    int i;

    // Debug
    TRACE("SemaphoreSignal(Value %" PRIiIN ")", Semaphore->Value);

    // assert not max
    CurrentValue = atomic_load(&Semaphore->Value);
    __STRICT_ASSERT((CurrentValue + Value) > Semaphore->MaxValue);
    if ((CurrentValue + Value) <= Semaphore->MaxValue) {
        for (i = 0; i < Value; i++) {
            while ((CurrentValue + 1) <= Semaphore->MaxValue) {
                if (atomic_compare_exchange_weak(&Semaphore->Value, &CurrentValue, CurrentValue + 1)) {
                    break;
                }
            }
            SchedulerHandleSignal((uintptr_t*)&Semaphore->Value);
        }
        Status = OsSuccess;
    }
    return Status;
}
