/* MollenOS
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
 * Synchronization (Counting Semaphores)
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
#include <assert.h>
#include <debug.h>
#include <futex.h>
#include <limits.h>
#include <semaphore.h>

void
SemaphoreConstruct(
    _In_ Semaphore_t* Semaphore,
    _In_ int          InitialValue,
    _In_ int          MaximumValue)
{
	assert(Semaphore != NULL);
	assert(InitialValue >= 0);
    assert(MaximumValue >= InitialValue);

	Semaphore->Value    = ATOMIC_VAR_INIT(InitialValue);
    Semaphore->MaxValue = MaximumValue;
}

OsStatus_t
SemaphoreDestruct(
    _In_ Semaphore_t* Semaphore)
{
    if (!Semaphore) {
        return OsInvalidParameters;
    }
    return FutexWake(&Semaphore->Value, INT_MAX, FUTEX_WAKE_PRIVATE);
}

OsStatus_t
SemaphoreWait(
    _In_ Semaphore_t* Semaphore,
    _In_ size_t       Timeout)
{
    OsStatus_t Status;
    int        Value = atomic_fetch_sub(&Semaphore->Value, 1) - 1;
    TRACE("SemaphoreWait(Timeout %" PRIiIN "): %i", Timeout, Value);
    
    // Essentially what we do here is make sure we wait untill the value is
    // either above/equal to zero or we were successfully woken up
    while (1) {
        if (Value >= 0) { // CurrentValue >= Value?? Oh right, that would give us FILO instead of FIFO 
            Status = OsSuccess;
            break;
        }

        // Go to sleep atomically, check return value, if there was sync
        // issues try again, OsError is returned.
        // Break out on timeouts and also successfully waiting. If we timeout we should
        // correct for the spot we are not using?
        Status = FutexWait(&Semaphore->Value, Value, FUTEX_WAIT_PRIVATE, Timeout);
        if (Status != OsError) {
            if (Status == OsTimeout) {
                atomic_fetch_add(&Semaphore->Value, 1);
            }
            break;
        }
        Value = atomic_load(&Semaphore->Value);
    }
    return Status;
}

OsStatus_t
SemaphoreSignal(
    _In_ Semaphore_t* Semaphore,
    _In_ int          Value)
{
    OsStatus_t Status = OsError;
    int        CurrentValue;
    int        i;

    TRACE("SemaphoreSignal(Value %" PRIiIN ")", Semaphore->Value);

    // assert not max
    CurrentValue = atomic_load(&Semaphore->Value);
    __STRICT_ASSERT((CurrentValue + Value) <= Semaphore->MaxValue);
    if ((CurrentValue + Value) <= Semaphore->MaxValue) {
        for (i = 0; i < Value; i++) {
            while ((CurrentValue + 1) <= Semaphore->MaxValue) {
                if (atomic_compare_exchange_weak(&Semaphore->Value, &CurrentValue, CurrentValue + 1)) {
                    break;
                }
            }
            FutexWake(&Semaphore->Value, 1, FUTEX_WAKE_PRIVATE);
        }
        Status = OsSuccess;
    }
    return Status;
}
