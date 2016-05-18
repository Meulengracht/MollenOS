/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* Semaphores
*/

/* Includes */
#include <assert.h>
#include <Scheduler.h>
#include <Semaphore.h>
#include <Heap.h>

/* Globals */
list_t *GlbUserSemaphores = NULL;
int GlbUserSemaphoreInit = 0;

/* This method allocates and constructs
 * a new semaphore handle. This is a kernel
 * semaphore */
Semaphore_t *SemaphoreCreate(int Value)
{
	Semaphore_t *Semaphore;

	/* Sanity */
	assert(Value >= 0);

	/* Allocate */
	Semaphore = (Semaphore_t*)kmalloc(sizeof(Semaphore_t));
	Semaphore->Value = Value;
	Semaphore->Creator = ThreadingGetCurrentThreadId();

	/* Setup lock */
	SpinlockReset(&Semaphore->Lock);

	return Semaphore;
}

/* This method allocates and constructs
 * a new semaphore handle. This is a usermode
 * semaphore */
UserSemaphore_t *SemaphoreUserCreate(MString_t *Identifier, int Value)
{
	/* First of all, make sure there is no 
	 * conflicting semaphores in system */
	if (Identifier != NULL) 
	{
		/* Init ? */
		if (GlbUserSemaphoreInit != 1) {
			GlbUserSemaphores = list_create(LIST_SAFE);
			GlbUserSemaphoreInit = 1;
		}

		/* Hash the string */
		size_t Hash = MStringHash(Identifier);

		/* Check list if exists */
		void *uSem = list_get_data_by_id(GlbUserSemaphores, (int)Hash, 0);

		/* Sanity */
		if (uSem != NULL)
			return (UserSemaphore_t*)uSem;
	}

	/* Allocate a new semaphore */
	UserSemaphore_t *Semaphore = (UserSemaphore_t*)kmalloc(sizeof(UserSemaphore_t));
	Semaphore->Identifier = Identifier;

	/* Construct */
	SemaphoreConstruct(&Semaphore->Semaphore, Value);

	/* Add to system list of semaphores if global */
	if (Identifier != NULL) 
	{
		/* Hash the string */
		size_t Hash = MStringHash(Identifier);

		/* Add to list */
		list_append(GlbUserSemaphores, list_create_node((int)Hash, Semaphore));
	}

	/* Done! */
	return Semaphore;
}

/* Destroys and frees a user semaphore, releasing any
 * resources associated with it */
void SemaphoreUserDestroy(UserSemaphore_t *Semaphore)
{
	/* Sanity 
	 * If it has a global identifier
	 * we want to remove it from list */
	if (Semaphore->Identifier != NULL) 
	{
		/* Hash the string */
		size_t Hash = MStringHash(Semaphore->Identifier);

		/* Remove id */
		list_remove_by_id(GlbUserSemaphores, (int)Hash);

		/* Cleanup Ident */
		MStringDestroy(Semaphore->Identifier);
	}

	/* Wake up all */
	SchedulerWakeupAllThreads((Addr_t*)&Semaphore->Semaphore);

	/* Free it */
	kfree(Semaphore);
}

/* This method constructs a new semaphore handle.
 * Does not allocate any memory
 * This is a kernel semaphore */
void SemaphoreConstruct(Semaphore_t *Semaphore, int Value)
{
	/* Sanity */
	assert(Value >= 0);

	/* Allocate */
	Semaphore = (Semaphore_t*)kmalloc(sizeof(Semaphore_t));
	Semaphore->Value = Value;
	Semaphore->Creator = ThreadingGetCurrentThreadId();

	/* Reset lock */
	SpinlockReset(&Semaphore->Lock);
}

/* Destroys and frees a semaphore, releasing any
 * resources associated with it */
void SemaphoreDestroy(Semaphore_t *Semaphore)
{
	/* Wake up all */
	SchedulerWakeupAllThreads((Addr_t*)Semaphore);

	/* Free it */
	kfree(Semaphore);
}

/* Semaphore Wait
 * Waits for the semaphore signal */
void SemaphoreP(Semaphore_t *Semaphore, size_t Timeout)
{
	/* Lock */
	SpinlockAcquire(&Semaphore->Lock);

	Semaphore->Value--;
	if (Semaphore->Value < 0)
	{
		/* Important to release lock before we do this */
		SpinlockRelease(&Semaphore->Lock);
		
		/* Go to sleep */
		SchedulerSleepThread((Addr_t*)Semaphore, Timeout);
		IThreadYield();
	}
	else
		SpinlockRelease(&Semaphore->Lock);
}

/* Semaphore Signal
 * Signals the semaphore */
void SemaphoreV(Semaphore_t *Semaphore)
{
	/* Lock */
	SpinlockAcquire(&Semaphore->Lock);

	/* Do Magic */
	Semaphore->Value++;
	if (Semaphore->Value <= 0)
		SchedulerWakeupOneThread((Addr_t*)Semaphore);

	/* Release */
	SpinlockRelease(&Semaphore->Lock);
}