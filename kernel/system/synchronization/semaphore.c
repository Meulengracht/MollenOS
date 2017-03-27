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
 * - Semaphores implementation, used safe passages known as
 *   critical sections in MCore
 */

/* Includes 
 * - System */
#include <system/thread.h>
#include <scheduler.h>
#include <semaphore.h>
#include <heap.h>

/* Includes
 * - Library */
#include <ds/list.h>
#include <stddef.h>
#include <assert.h>

/* Globals */
List_t *GlbSemaphores = NULL;
int GlbSemaphoreInit = 0;

/* SemaphoreCreate
 * Initializes and allocates a new semaphore
 * Semaphores use safe passages to avoid race-conditions */
Semaphore_t *SemaphoreCreate(int InitialValue)
{
	/* Variables */
	Semaphore_t *Semaphore = NULL;

	/* Allocate a new instance of a semaphore 
	 * and instantiate it */
	Semaphore = (Semaphore_t*)kmalloc(sizeof(Semaphore_t));
	SemaphoreConstruct(Semaphore, InitialValue);

	/* Done! */
	return Semaphore;
}

/* SemaphoreCreateGlobal
 * Creates a global semaphore, identified by it's name
 * and makes sure only one can exist at the time. Returns
 * NULL if one already exists. */
Semaphore_t *SemaphoreCreateGlobal(MString_t *Identifier, int InitialValue)
{
	/* Variables */
	DataKey_t hKey;

	/* First of all, make sure there is no 
	 * conflicting semaphores in system */
	if (Identifier != NULL) {
		if (GlbSemaphoreInit != 1) {
			GlbSemaphores = ListCreate(KeyInteger, LIST_SAFE);
			GlbSemaphoreInit = 1;
		}

		/* Hash the string */
		hKey.Value = (int)MStringHash(Identifier);

		/* Check list if exists */
		void *Exists = ListGetDataByKey(GlbSemaphores, hKey, 0);

		/* Sanitize the lookup */
		if (Exists != NULL) {
			return NULL;
		}
	}

	/* Allocate a new semaphore */
	Semaphore_t *Semaphore = (Semaphore_t*)kmalloc(sizeof(Semaphore_t));
	SemaphoreConstruct(Semaphore, InitialValue);
	Semaphore->Hash = MStringHash(Identifier);

	/* Add to system list of semaphores if global */
	if (Identifier != NULL)  {
		ListAppend(GlbSemaphores, ListCreateNode(hKey, hKey, Semaphore));
	}

	/* Done! */
	return Semaphore;
}

/* SemaphoreDestroy
 * Destroys and frees a semaphore, releasing any
 * resources associated with it */
void SemaphoreDestroy(Semaphore_t *Semaphore)
{
	/* Variables */
	DataKey_t Key;

	/* Sanity 
	 * If it has a global identifier
	 * we want to remove it from list */
	if (Semaphore->Hash != 0) {
		Key.Value = (int)Semaphore->Hash;
		ListRemoveByKey(GlbSemaphores, Key);
	}

	/* Wake up all */
	SchedulerWakeupAllThreads((uintptr_t*)Semaphore);

	/* Free it */
	kfree(Semaphore);
}

/* SemaphoreConstruct
 * Constructs an already allocated semaphore and resets
 * it's value to the given initial value */
void SemaphoreConstruct(Semaphore_t *Semaphore, int InitialValue)
{
	/* Sanitize the parameters */
	assert(Semaphore != NULL);
	assert(InitialValue >= 0);

	/* Reset values to initial stuff */
	Semaphore->Hash = 0;
	Semaphore->Value = InitialValue;
	Semaphore->Creator = ThreadingGetCurrentThreadId();

	/* Reset safe passage */
	CriticalSectionConstruct(&Semaphore->Lock, CRITICALSECTION_PLAIN);
}

/* SemaphoreP (Wait) 
 * Waits for the semaphore signal with the optional time-out */
void SemaphoreP(Semaphore_t *Semaphore, size_t Timeout)
{
	/* Enter the safe-passage, we do not wish
	 * to be interrupted while doing this */
	CriticalSectionEnter(&Semaphore->Lock);

	/* Decrease the value, and do the sanity check 
	 * if we should sleep for events */
	Semaphore->Value--;
	if (Semaphore->Value < 0) {
		/* It's important we leave safe-mode before
		 * waking up others, otherwise lock is kept */
		CriticalSectionLeave(&Semaphore->Lock);
		SchedulerSleepThread((uintptr_t*)Semaphore, Timeout);
		IThreadYield();
	}
	else {
		/* Make sure to leave safe passage again! */
		CriticalSectionLeave(&Semaphore->Lock);
	}
}

/* SemaphoreV (Signal) 
 * Signals the semaphore with the given value, default is 1 */
void SemaphoreV(Semaphore_t *Semaphore, int Value)
{
	/* Enter the safe-passage, we do not wish
	 * to be interrupted while doing this */
	CriticalSectionEnter(&Semaphore->Lock);

	/* Increase by the given value 
	 * and check if we should wake up others */
	Semaphore->Value += Value;
	if (Semaphore->Value <= 0) {
		SchedulerWakeupOneThread((uintptr_t*)Semaphore);
	}

	/* Make sure to leave safe passage again! */
	CriticalSectionLeave(&Semaphore->Lock);
}
