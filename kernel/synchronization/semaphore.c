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
#define __MODULE "SEM0"
//#define __TRACE
#ifdef __TRACE
#define __STRICT_ASSERT(x) assert(x)
#else
#define __STRICT_ASSERT(x) 
#endif

#include <system/thread.h>
#include <system/utils.h>
#include <scheduler.h>
#include <semaphore.h>
#include <debug.h>
#include <heap.h>

#include <ds/collection.h>
#include <stddef.h>
#include <assert.h>

/* Globals */
static Collection_t Semaphores = COLLECTION_INIT(KeyInteger);

/* SemaphoreCreate
 * Initializes and allocates a new semaphore
 * Semaphores use safe passages to avoid race-conditions */
Semaphore_t*
SemaphoreCreate(
    _In_ int        InitialValue,
    _In_ int        MaximumValue)
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
    _In_ MString_t* Identifier, 
    _In_ int        InitialValue,
    _In_ int        MaximumValue)
{
	/* Variables */
	DataKey_t hKey;

	/* First of all, make sure there is no 
	 * conflicting semaphores in system */
	if (Identifier != NULL) {
		hKey.Value      = (int)MStringHash(Identifier);
		void *Exists    = CollectionGetDataByKey(&Semaphores, hKey, 0);
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
		CollectionAppend(&Semaphores, CollectionCreateNode(hKey, Semaphore));
	}
	return Semaphore;
}

/* SemaphoreDestroy
 * Destroys and frees a semaphore, releasing any
 * resources associated with it */
void
SemaphoreDestroy(
    _In_ Semaphore_t*   Semaphore)
{
	// Variables
	DataKey_t Key;
	if (Semaphore->Hash != 0) {
		Key.Value = (int)Semaphore->Hash;
		CollectionRemoveByKey(&Semaphores, Key);
	}
	SchedulerHandleSignalAll((uintptr_t*)Semaphore);
    if (Semaphore->Cleanup) {
	    kfree(Semaphore);
    }
}

/* SemaphoreConstruct
 * Constructs an already allocated semaphore and resets
 * it's value to the given initial value */
void
SemaphoreConstruct(
    _In_ Semaphore_t*   Semaphore,
    _In_ int            InitialValue,
    _In_ int            MaximumValue)
{
	// Sanitize values
	assert(Semaphore != NULL);
	assert(InitialValue >= 0);
    assert(MaximumValue >= InitialValue);

	// Initiate members
    memset((void*)Semaphore, 0, sizeof(Semaphore_t));
    Semaphore->MaxValue = MaximumValue;
	Semaphore->Value    = InitialValue;
	Semaphore->Creator  = ThreadingGetCurrentThreadId();
}

/* SemaphoreWait
 * Waits for the semaphore signal with the optional time-out.
 * Returns SCHEDULER_SLEEP_OK or SCHEDULER_SLEEP_TIMEOUT */
int
SemaphoreWait(
    _In_ Semaphore_t*   Semaphore,
    _In_ size_t         Timeout)
{
	Semaphore->Value--;
	if (Semaphore->Value < 0) {
        int SleepStatus = SchedulerThreadSleep((uintptr_t*)Semaphore, Timeout);
        if (SleepStatus == SCHEDULER_SLEEP_TIMEOUT) {
            Semaphore->Value++;
            return SCHEDULER_SLEEP_TIMEOUT;
        }
        return SCHEDULER_SLEEP_OK;
	}
    return SCHEDULER_SLEEP_OK;
}

/* SemaphoreSignal
 * Signals the semaphore with the given value, default is 1 */
OsStatus_t
SemaphoreSignal(
    _In_ Semaphore_t*   Semaphore,
    _In_ int            Value)
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
                do {
                    Status = SchedulerHandleSignal((uintptr_t*)Semaphore);
                }
                while (Status != OsSuccess);
            }
        }
        Status = OsSuccess;
    }
    AtomicSectionLeave(&Semaphore->SyncObject);
    return Status;
}
