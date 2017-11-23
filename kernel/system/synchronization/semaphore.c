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
 * - Semaphores implementation, lockless implementation.
 */
#define __MODULE "SEM0"
//#define __TRACE

/* Includes 
 * - System */
#include <system/thread.h>
#include <system/utils.h>
#include <scheduler.h>
#include <semaphore.h>
#include <debug.h>
#include <heap.h>

/* Includes
 * - Library */
#include <ds/collection.h>
#include <stddef.h>
#include <assert.h>

/* Globals */
static Collection_t *GlbSemaphores = NULL;
static Spinlock_t SemaphoreLock;
int GlbSemaphoreInit = 0;

/* SemaphoreCreate
 * Initializes and allocates a new semaphore
 * Semaphores use safe passages to avoid race-conditions */
Semaphore_t*
SemaphoreCreate(
    _In_ int InitialValue,
    _In_ int MaximumValue)
{
    // Variables
	Semaphore_t *Semaphore = NULL;

    // Initialize the new instance
	Semaphore = (Semaphore_t*)kmalloc(sizeof(Semaphore_t));
	SemaphoreConstruct(Semaphore, InitialValue, MaximumValue);
    Semaphore->Cleanup = 1;
	return Semaphore;
}

/* SemaphoreCreateGlobal
 * Creates a global semaphore, identified by it's name
 * and makes sure only one can exist at the time. Returns
 * NULL if one already exists. */
Semaphore_t*
SemaphoreCreateGlobal(
    _In_ MString_t  *Identifier, 
    _In_ int         InitialValue,
    _In_ int         MaximumValue)
{
	/* Variables */
	DataKey_t hKey;

	/* First of all, make sure there is no 
	 * conflicting semaphores in system */
	if (Identifier != NULL) {
		if (GlbSemaphoreInit != 1) {
			GlbSemaphores = CollectionCreate(KeyInteger);
			GlbSemaphoreInit = 1;
            SpinlockReset(&SemaphoreLock);
		}

		/* Hash the string */
		hKey.Value = (int)MStringHash(Identifier);

		/* Check list if exists */
        SpinlockAcquire(&SemaphoreLock);
		void *Exists = CollectionGetDataByKey(GlbSemaphores, hKey, 0);
        SpinlockRelease(&SemaphoreLock);

		/* Sanitize the lookup */
		if (Exists != NULL) {
			return NULL;
		}
	}

	/* Allocate a new semaphore */
	Semaphore_t *Semaphore = (Semaphore_t*)kmalloc(sizeof(Semaphore_t));
	SemaphoreConstruct(Semaphore, InitialValue, MaximumValue);
	Semaphore->Hash = MStringHash(Identifier);

	/* Add to system list of semaphores if global */
	if (Identifier != NULL)  {
        SpinlockAcquire(&SemaphoreLock);
		CollectionAppend(GlbSemaphores, CollectionCreateNode(hKey, Semaphore));
        SpinlockRelease(&SemaphoreLock);
	}
	return Semaphore;
}

/* SemaphoreDestroy
 * Destroys and frees a semaphore, releasing any
 * resources associated with it */
void
SemaphoreDestroy(
    _In_ Semaphore_t *Semaphore)
{
	// Variables
	DataKey_t Key;
	if (Semaphore->Hash != 0) {
		Key.Value = (int)Semaphore->Hash;
		CollectionRemoveByKey(GlbSemaphores, Key);
	}
	SchedulerThreadWakeAll((uintptr_t*)Semaphore);
    if (Semaphore->Cleanup) {
	    kfree(Semaphore);
    }
}

/* SemaphoreConstruct
 * Constructs an already allocated semaphore and resets
 * it's value to the given initial value */
void
SemaphoreConstruct(
    _In_ Semaphore_t    *Semaphore,
    _In_ int             InitialValue,
    _In_ int             MaximumValue)
{
	// Sanitize values
	assert(Semaphore != NULL);
	assert(InitialValue >= 0);
    assert(MaximumValue >= InitialValue);

	// Initiate members
    Semaphore->MaxValue = MaximumValue;
	Semaphore->Value = ATOMIC_VAR_INIT(InitialValue);
	Semaphore->Creator = ThreadingGetCurrentThreadId();
	Semaphore->Hash = 0;
    Semaphore->Cleanup = 0;
}

/* SemaphoreWait
 * Waits for the semaphore signal with the optional time-out.
 * Returns SCHEDULER_SLEEP_OK or SCHEDULER_SLEEP_TIMEOUT */
int
SemaphoreWait(
    _In_ Semaphore_t    *Semaphore,
    _In_ size_t          Timeout)
{
    // Variables
    int UpdatedValue = 0;

	// Decrease the value, and do the sanity check 
	// if we should sleep for events
    UpdatedValue = atomic_fetch_sub(&Semaphore->Value, 1);
    
    // Debug
    TRACE("SemaphoreWait(Value %i)", UpdatedValue);
	UpdatedValue--;
	if (UpdatedValue < 0) {
        if (SchedulerThreadSleep((uintptr_t*)Semaphore, Timeout) == SCHEDULER_SLEEP_TIMEOUT) {
            atomic_fetch_add(&Semaphore->Value, 1);
            return SCHEDULER_SLEEP_TIMEOUT;
        }
	}
    return SCHEDULER_SLEEP_OK;
}

/* SemaphoreSignal
 * Signals the semaphore with the given value, default is 1 */
void
SemaphoreSignal(
    _In_ Semaphore_t    *Semaphore,
    _In_ int             Value)
{
	// Variables
    int CurrentValue = atomic_load(&Semaphore->Value);
    int i;

    // Debug
    TRACE("SemaphoreSignal(Value %i)", CurrentValue);

    // assert not max
    assert(CurrentValue != Semaphore->MaxValue);
    for (i = 0; i < Value; i++) {
        CurrentValue = atomic_fetch_add(&Semaphore->Value, 1);
        CurrentValue++;
        if (CurrentValue <= 0) {
            SchedulerThreadWake((uintptr_t*)Semaphore);
        }
        if (CurrentValue == Semaphore->MaxValue) {
            break;
        }
    }
}
