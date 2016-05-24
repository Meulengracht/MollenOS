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
* MollenOS Threading Scheduler
* Implements scheduling with priority
* Priority 61 is System Priority.
* Priority 60 - 0 are Normal Priorties
* Priorities 60 - 0 start at 10 ms, slowly increases to 130 ms.
* Priority boosts every 1000 ms? 
* On yields, keep priority.
* On task-switchs, decrease priority.
* A thread can only stay a maximum in each priority.
*/

/* Includes */
#include <Arch.h>
#include <Scheduler.h>
#include <Threading.h>
#include <List.h>
#include <Heap.h>
#include <Log.h>

/* CLib */
#include <assert.h>
#include <stdio.h>

/* Globals */
Scheduler_t *GlbSchedulers[MCORE_MAX_SCHEDULERS];
list_t *IoQueue = NULL;
list_t *GlbSchedulerNodeRecycler = NULL;
int GlbSchedulerEnabled = 0;

/* This initializes the scheduler for the
 * given cpu_id, the first call to this
 * will also initialize the scheduler enviornment */
void SchedulerInit(Cpu_t Cpu)
{
	/* Variables needed */
	Scheduler_t *Scheduler;
	int i;

	/* Is this BSP setting up? */
	if (Cpu == 0)
	{
		/* Null out stuff */
		for (i = 0; i < MCORE_MAX_SCHEDULERS; i++)
			GlbSchedulers[i] = NULL;

		/* Allocate IoQueue */
		IoQueue = list_create(LIST_SAFE);

		/* Allocate Node Recycler */
		GlbSchedulerNodeRecycler = list_create(LIST_SAFE);
	}

	/* Allocate a scheduler */
	Scheduler = (Scheduler_t*)kmalloc(sizeof(Scheduler_t));

	/* Init thread queue */
	Scheduler->Threads = list_create(LIST_NORMAL);

	/* Initialize all queues (Lock-Less) */
	for (i = 0; i < MCORE_SCHEDULER_LEVELS; i++)
		Scheduler->Queues[i] = list_create(LIST_NORMAL);

	/* Reset boost timer */
	Scheduler->BoostTimer = 0;
	Scheduler->NumThreads = 0;

	/* Reset lock */
	SpinlockReset(&Scheduler->Lock);

	/* Enable */
	GlbSchedulers[Cpu] = Scheduler;
	GlbSchedulerEnabled = 1;
}

/* Boost ALL threads to priority queue 0 */
void SchedulerBoost(Scheduler_t *Scheduler)
{
	/* Variables */
	MCoreThread_t *mThread;
	list_node_t *mNode;
	int i = 0;
	
	/* Step 1. Loop through all queues, pop their elements and append them to queue 0
	 * Reset time-slices */
	for (i = 1; i < MCORE_SCHEDULER_LEVELS; i++)
	{
		if (Scheduler->Queues[i]->length > 0)
		{
			mNode = list_pop_front(Scheduler->Queues[i]);

			while (mNode != NULL)
			{
				/* Reset timeslice */
				mThread = (MCoreThread_t*)mNode->data;
				mThread->TimeSlice = MCORE_INITIAL_TIMESLICE;
				mThread->Priority = 0;

				list_append(Scheduler->Queues[0], mNode);
				mNode = list_pop_front(Scheduler->Queues[i]);
			}
		}
	}
}

/* Allocate a Queue Node */
list_node_t *SchedulerGetNode(int Id, void *Data)
{
	/* Sanity */
	if (GlbSchedulerNodeRecycler != NULL) 
	{
		/* Pop first */
		list_node_t *RetNode = list_pop_front(GlbSchedulerNodeRecycler);

		/* Sanity */
		if (RetNode == NULL)
			return list_create_node(Id, Data);
		
		/* Set data */
		RetNode->identifier = Id;
		RetNode->data = Data;
		return RetNode;
	}
	else
		return list_create_node(Id, Data);
}

/* This function arms a thread for scheduling
 * in most cases this is called with a prefilled
 * priority of -1 to make it run almost immediately */
void SchedulerReadyThread(MCoreThread_t *Thread)
{
	/* Add task to a queue based on its priority */
	list_node_t *ThreadNode;
	Cpu_t CpuIndex = 0;
	int i = 0;
	
	/* Step 1. New thread? :) */
	if (Thread->Priority == -1)
	{
		/* Reduce priority */
		Thread->Priority = 0;

		/* Recalculate time-slice */
		Thread->TimeSlice = MCORE_INITIAL_TIMESLICE;
	}

	/* Step 2. Find the least used CPU */
	if (Thread->CpuId == 0xFF)
	{
		/* Yea, broadcast thread 
		 * Locate the least used CPU 
		 * TODO */
		while (GlbSchedulers[i] != NULL)
		{
			if (GlbSchedulers[i]->NumThreads < GlbSchedulers[CpuIndex]->NumThreads)
				CpuIndex = i;

			i++;
		}

		/* Now lock the cpu at that core for now */
		Thread->CpuId = CpuIndex;
	}
	else
	{
		/* Add it to appropriate list */
		CpuIndex = Thread->CpuId;
	}

	/* Get thread node if exists */
	ThreadNode = list_get_node_by_id(GlbSchedulers[CpuIndex]->Threads, Thread->ThreadId, 0);

	/* Sanity */
	if (ThreadNode == NULL) {
		ThreadNode = list_create_node(Thread->ThreadId, Thread);
		list_append(GlbSchedulers[CpuIndex]->Threads, ThreadNode);
		GlbSchedulers[CpuIndex]->NumThreads++;
	}

	/* Get lock */
	SpinlockAcquire(&GlbSchedulers[CpuIndex]->Lock);

	/* Append */
	list_append(GlbSchedulers[CpuIndex]->Queues[Thread->Priority], 
		SchedulerGetNode(Thread->ThreadId, Thread));

	/* Release lock */
	SpinlockRelease(&GlbSchedulers[CpuIndex]->Lock);

	/* Wakeup CPU if sleeping */
	if (ThreadingIsCurrentTaskIdle(Thread->CpuId) != 0)
		ThreadingWakeCpu(Thread->CpuId);
}

/* Disarms a thread from queues */
void SchedulerDisarmThread(MCoreThread_t *Thread)
{
	/* Vars */
	list_node_t *ThreadNode;

	/* Sanity */
	if (Thread->Priority < 0)
		return;

	/* Get lock */
	SpinlockAcquire(&GlbSchedulers[Thread->CpuId]->Lock);

	/* Find */
	ThreadNode = list_get_node_by_id(
		GlbSchedulers[Thread->CpuId]->Queues[Thread->Priority], Thread->ThreadId, 0);

	/* Remove it */
	if (ThreadNode != NULL) {
		list_remove_by_node(GlbSchedulers[Thread->CpuId]->Queues[Thread->Priority], ThreadNode);
		list_append(GlbSchedulerNodeRecycler, ThreadNode);
	}

	/* Release lock */
	SpinlockRelease(&GlbSchedulers[Thread->CpuId]->Lock);
}

/* This function is primarily used to remove a thread from
 * scheduling totally, but it can always be scheduld again
 * by calling SchedulerReadyThread */
void SchedulerRemoveThread(MCoreThread_t *Thread)
{
	/* Get thread node if exists */
	list_node_t *ThreadNode = 
		list_get_node_by_id(GlbSchedulers[Thread->CpuId]->Threads, Thread->ThreadId, 0);

	/* Sanity */
	assert(ThreadNode != NULL);

	/* Disarm the thread */
	SchedulerDisarmThread(Thread);

	/* Remove the node */
	list_remove_by_node(GlbSchedulers[Thread->CpuId]->Threads, ThreadNode);
	GlbSchedulers[Thread->CpuId]->NumThreads--;

	/* Free */
	kfree(ThreadNode);
}

/* This is used by timer code to reduce threads's timeout
 * if this function wasn't called then sleeping threads and 
 * waiting threads would never be armed again. */
void SchedulerApplyMs(size_t Ms)
{
	/* Find first thread matching resource */
	list_node_t *sNode = NULL;

	/* Sanity */
	if (IoQueue == NULL
		|| GlbSchedulerEnabled != 1)
		return;

	/* Loop */
	for (sNode = IoQueue->head; sNode != NULL;)
	{
		/* Cast */
		MCoreThread_t *mThread = (MCoreThread_t*)sNode->data;

		/* Sanity */
		if (mThread->Sleep != 0) 
		{
			/* Reduce */
			mThread->Sleep -= MIN(Ms, mThread->Sleep);

			/* Time to wakeup? */
			if (mThread->Sleep == 0)
			{
				/* Temporary pointer */
				list_node_t *WakeNode = sNode;

				/* Skip to next node */
				sNode = sNode->link;

				/* Store node */
				list_append(GlbSchedulerNodeRecycler, WakeNode);

				/* Grant it top priority */
				mThread->Priority = -1;
				mThread->SleepResource = NULL;

				/* Rearm thread */
				SchedulerReadyThread(mThread);
			}
			else {
				/* Go to next */
				sNode = sNode->link;
			}
		}
		else {
			/* Go to next */
			sNode = sNode->link;
		}
	}
}

/* This function sleeps the current thread either by resource,
 * by time, or both. If resource is NULL then it will wake the
 * thread after <timeout> ms. If infinite wait is required set
 * timeout to 0 */
void SchedulerSleepThread(Addr_t *Resource, size_t Timeout)
{
	/* Disable Interrupts 
	 * This is a fragile operation */
	MCoreThread_t *CurrentThread = NULL;
	IntStatus_t IntrState = InterruptDisable();
	Cpu_t Cpu = ApicGetCpu();

	/* Mark current thread for sleep */
	CurrentThread = ThreadingGetCurrentThread(Cpu);

	/* Mark for sleep */
	CurrentThread->Flags |= THREADING_ENTER_SLEEP;

	/* Disarm the thread */
	SchedulerDisarmThread(CurrentThread);

	/* Add to list */
	CurrentThread->SleepResource = Resource;
	CurrentThread->Sleep = Timeout;
	list_append(IoQueue,
		SchedulerGetNode(CurrentThread->ThreadId, CurrentThread));

	/* Restore interrupts */
	InterruptRestoreState(IntrState);
}

/* These two functions either wakes one or all threads
 * waiting for a resource. */
int SchedulerWakeupOneThread(Addr_t *Resource)
{
	/* Find first thread matching resource */
	list_node_t *SleepMatch = NULL;

	/* Sanity */
	if (IoQueue == NULL
		|| GlbSchedulerEnabled != 1)
		return 0;

	/* Loop */
	foreach(i, IoQueue)
	{
		/* Cast */
		MCoreThread_t *mThread = (MCoreThread_t*)i->data;

		/* Did we find it? */
		if (mThread->SleepResource == Resource)
		{
			SleepMatch = i;
			break;
		}
	}

	/* Sanity */
	if (SleepMatch != NULL)
	{
		/* Cast data */
		MCoreThread_t *mThread = (MCoreThread_t*)SleepMatch->data;
		
		/* Cleanup */
		list_remove_by_node(IoQueue, SleepMatch);
		list_append(GlbSchedulerNodeRecycler, SleepMatch);

		/* Grant it top priority */
		mThread->Priority = -1;
		mThread->SleepResource = NULL;

		/* Rearm thread */
		SchedulerReadyThread(mThread);

		/* Done! */
		return 1;
	}
	else
		return 0;
}

/* These two functions either wakes one or all threads
 * waiting for a resource. */
void SchedulerWakeupAllThreads(Addr_t *Resource)
{
	/* Just keep iterating till no more sleep-matches */
	while (1)
	{
		/* Break out on a zero */
		if (!SchedulerWakeupOneThread(Resource))
			break;
	}
}

/* Schedule 
 * This should be called by the underlying archteicture code
 * to get the next thread that is to be run. */
MCoreThread_t *SchedulerGetNextTask(Cpu_t Cpu, MCoreThread_t *Thread, int PreEmptive)
{
	/* Variables */
	list_node_t *NextThreadNode = NULL;
	size_t TimeSlice;
	int i;

	/* Sanity */
	if (GlbSchedulerEnabled != 1 || GlbSchedulers[Cpu] == NULL)
		return Thread;

	/* Get the time delta */
	if (Thread != NULL)
	{
		/* Get time-slice */
		TimeSlice = Thread->TimeSlice;

		/* Increase its priority */
		if (PreEmptive != 0
			&& Thread->Priority < MCORE_SYSTEM_QUEUE) /* Must be below 60, 60 is highest normal queue */
		{
			/* Reduce priority */
			Thread->Priority++;

			/* Recalculate time-slice */
			Thread->TimeSlice = (Thread->Priority * 2) + MCORE_INITIAL_TIMESLICE;
		}

		/* Schedúle */
		SchedulerReadyThread(Thread);
	}
	else
		TimeSlice = MCORE_INITIAL_TIMESLICE;

	/* Acquire Lock */
	SpinlockAcquire(&GlbSchedulers[Cpu]->Lock);

	/* Append time-slice to current boost-watch */
	GlbSchedulers[Cpu]->BoostTimer += TimeSlice;

	/* Time for a turbo boost? :O */
	if (GlbSchedulers[Cpu]->BoostTimer >= MCORE_SCHEDULER_BOOST_MS)
	{
		/* Boost! */
		SchedulerBoost(GlbSchedulers[Cpu]);

		/* Reset to 0 */
		GlbSchedulers[Cpu]->BoostTimer = 0;
	}

	/* Step 4. Get next node */
	for (i = 0; i < MCORE_SCHEDULER_LEVELS; i++)
	{
		/* Do we even have any nodes in here ? */
		if (GlbSchedulers[Cpu]->Queues[i]->length > 0)
		{
			/* FIFO algorithm in queues */
			NextThreadNode = list_pop_front(GlbSchedulers[Cpu]->Queues[i]);

			/* Sanity */
			if (NextThreadNode != NULL)
				break;
		}
	}

	/* Release Lock */
	SpinlockRelease(&GlbSchedulers[Cpu]->Lock);

	/* Return */
	if (NextThreadNode == NULL)
		return NULL;
	else {
		/* Save node */
		list_append(GlbSchedulerNodeRecycler, NextThreadNode);

		/* Return */
		return (MCoreThread_t*)NextThreadNode->data;
	}
}