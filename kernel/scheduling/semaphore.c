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

oserr_t
SemaphoreDestruct(
    _In_ Semaphore_t* Semaphore)
{
    if (!Semaphore) {
        return OS_EINVALPARAMS;
    }
    return FutexWake(&Semaphore->Value, INT_MAX, 0);
}

oserr_t
SemaphoreWait(
    _In_ Semaphore_t* Semaphore,
    _In_ size_t       Timeout)
{
    oserr_t Status = OS_EOK;
    int        Value;
    
    for (;;) {
        Value = atomic_load(&(Semaphore->Value));
        while (Value < 1) {
            Status = FutexWait(&(Semaphore->Value), Value, 0, Timeout);
            if (Status != OS_EOK) {
                break;
            }
            Value = atomic_load(&(Semaphore->Value));
        }
        
        Value = atomic_fetch_sub(&(Semaphore->Value), 1);
        if (Value >= 1) {
            break;
        }
        atomic_fetch_add(&(Semaphore->Value), 1);
    }
    return Status;
}

oserr_t
SemaphoreSignal(
    _In_ Semaphore_t* Semaphore,
    _In_ int          Value)
{
    oserr_t Status = OS_EUNKNOWN;
    int        currentValue;
    int        i;
    int        result;

    TRACE("SemaphoreSignal(Value %" PRIiIN ")", Semaphore->Value);

    // assert not max
    currentValue = atomic_load(&Semaphore->Value);
    __STRICT_ASSERT((currentValue + Value) <= Semaphore->MaxValue);
    if (currentValue < Semaphore->MaxValue) {
        for (i = 0; i < Value; i++) {
            result = 0;
            while (!result && currentValue < Semaphore->MaxValue) {
                result = atomic_compare_exchange_weak(&Semaphore->Value,
                                                      &currentValue, currentValue + 1);
            }
            FutexWake(&Semaphore->Value, 1, 0);
        }
        Status = OS_EOK;
    }
    return Status;
}
