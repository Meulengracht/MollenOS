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

#include <system/thread.h>
#include <system/utils.h>
#include <semaphore_slim.h>
#include <scheduler.h>
#include <debug.h>
#include <heap.h>

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
	SchedulerHandleSignalAll((uintptr_t*)Semaphore);
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
    memset((void*)Semaphore, 0, sizeof(SlimSemaphore_t));
    Semaphore->MaxValue = MaximumValue;
	Semaphore->Value    = InitialValue;
}

/* SlimSemaphoreWait
 * Waits for the semaphore signal with the optional time-out.
 * Returns SCHEDULER_SLEEP_OK or SCHEDULER_SLEEP_TIMEOUT */
int
SlimSemaphoreWait(
    _In_ SlimSemaphore_t*   Semaphore,
    _In_ size_t             Timeout)
{
    AtomicSectionEnter(&Semaphore->SyncObject);
	Semaphore->Value--;
	if (Semaphore->Value < 0) {
        int SleepStatus = SchedulerAtomicThreadSleep(
            (uintptr_t*)Semaphore, Timeout, &Semaphore->SyncObject);
        if (SleepStatus == SCHEDULER_SLEEP_TIMEOUT) {
            Semaphore->Value++;
            return SCHEDULER_SLEEP_TIMEOUT;
        }
        return SCHEDULER_SLEEP_OK;
	}
    AtomicSectionLeave(&Semaphore->SyncObject);
    return SCHEDULER_SLEEP_OK;
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
    int i;

    // Debug
    TRACE("SemaphoreSignal(Value %i)", Semaphore->Value);

    // assert not max
    AtomicSectionEnter(&Semaphore->SyncObject);
    __STRICT_ASSERT((Semaphore->Value + Value) > Semaphore->MaxValue);
    if ((Semaphore->Value + Value) <= Semaphore->MaxValue) {
        for (i = 0; i < Value; i++) {
            Semaphore->Value++;
            if (Semaphore->Value <= 0) {
                SchedulerHandleSignal((uintptr_t*)Semaphore);
            }
            if (Semaphore->Value == Semaphore->MaxValue) {
                break;
            }
        }
        Status = OsSuccess;
    }
    AtomicSectionLeave(&Semaphore->SyncObject);
    return Status;
}
