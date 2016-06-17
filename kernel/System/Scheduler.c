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
#include <Heap.h>
#include <Log.h>

/* CLib */
#include <stddef.h>
#include <ds/list.h>
#include <assert.h>
#include <stdio.h>

/* Globals */
Scheduler_t *GlbSchedulers[MCORE_MAX_SCHEDULERS];
List_t *IoQueue = NULL;
List_t *GlbSchedulerNodeRecycler = NULL;
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
		IoQueue = ListCreate(KeyInteger, LIST_SAFE);

		/* Allocate Node Recycler */
		GlbSchedulerNodeRecycler = ListCreate(KeyInteger, LIST_SAFE);
	}

	/* Allocate a scheduler */
	Scheduler = (Scheduler_t*)kmalloc(sizeof(Scheduler_t));

	/* Init thread queue */
	Scheduler->Threads = ListCreate(KeyInteger, LIST_NORMAL);

	/* Initialize all queues (Lock-Less) */
	for (i = 0; i < MCORE_SCHEDULER_LEVELS; i++)
		Scheduler->Queues[i] = ListCreate(KeyInteger, LIST_NORMAL);

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
	ListNode_t *mNode;
	int i = 0;
	
	/* Step 1. Loop through all queues, pop their elements and append them to queue 0
	 * Reset time-slices */
	for (i = 1; i < MCORE_SCHEDULER_LEVELS; i++)
	{
		if (Scheduler->Queues[i]->Length > 0)
		{
			mNode = ListPopFront(Scheduler->Queues[i]);

			while (mNode != NULL)
			{
				/* Reset timeslice */
				mThread = (MCoreThread_t*)mNode->Data;
				mThread->TimeSlice = MCORE_INITIAL_TIMESLICE;
				mThread->Queue = 0;

				ListAppend(Scheduler->Queues[0], mNode);
				mNode = ListPopFront(Scheduler->Queues[i]);
			}
		}
	}
}

/* Allocate a Queue Node */
ListNode_t *SchedulerGetNode(DataKey_t Key, DataKey_t SortKey, void *Data)
{
	/* Sanity */
	if (GlbSchedulerNodeRecycler != NULL) 
	{
		/* Pop first */
		ListNode_t *RetNode = ListPopFront(GlbSchedulerNodeRecycler);

		/* Sanity */
		if (RetNode == NULL)
			return ListCreateNode(Key, SortKey, Data);
		
		/* Set data */
		RetNode->Key = Key;
		RetNode->SortKey = SortKey;
		RetNode->Data = Data;
		return RetNode;
	}
	else
		return ListCreateNode(Key, SortKey, Data);
}

/* This function arms a thread for scheduling
 * in most cases this is called with a prefilled
 * priority of -1 to make it run almost immediately */
void SchedulerReadyThread(MCoreThread_t *Thread)
{
	/* Add task to a queue based on its priority */
	ListNode_t *ThreadNode;
	Cpu_t CpuIndex = 0;
	DataKey_t iKey;
	DataKey_t sKey;
	int i = 0;
	
	/* Step 1. New thread? :) */
	if (Thread->Queue == -1)
	{
		/* Reduce priority */
		Thread->Queue = 0;

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

	/* Setup keys */
	iKey.Value = (int)Thread->ThreadId;
	sKey.Value = (int)Thread->Priority;

	/* Get thread node if exists */
	ThreadNode = ListGetNodeByKey(GlbSchedulers[CpuIndex]->Threads, iKey, 0);

	/* Sanity */
	if (ThreadNode == NULL) {
		ThreadNode = ListCreateNode(iKey, sKey, Thread);
		ListAppend(GlbSchedulers[CpuIndex]->Threads, ThreadNode);
		GlbSchedulers[CpuIndex]->NumThreads++;
	}

	/* Get lock */
	SpinlockAcquire(&GlbSchedulers[CpuIndex]->Lock);

	/* Append */
	ListAppend(GlbSchedulers[CpuIndex]->Queues[Thread->Queue],
		SchedulerGetNode(iKey, sKey, Thread));

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
	ListNode_t *ThreadNode;
	DataKey_t iKey;

	/* Sanity */
	if (Thread->Queue < 0)
		return;

	/* Get lock */
	SpinlockAcquire(&GlbSchedulers[Thread->CpuId]->Lock);

	/* Find */
	iKey.Value = (int)Thread->ThreadId;
	ThreadNode = ListGetNodeByKey(
		GlbSchedulers[Thread->CpuId]->Queues[Thread->Queue], iKey, 0);

	/* Remove it */
	if (ThreadNode != NULL) {
		ListRemoveByNode(GlbSchedulers[Thread->CpuId]->Queues[Thread->Queue], ThreadNode);
		ListAppend(GlbSchedulerNodeRecycler, ThreadNode);
	}

	/* Release lock */
	SpinlockRelease(&GlbSchedulers[Thread->CpuId]->Lock);
}

/* This function is primarily used to remove a thread from
 * scheduling totally, but it can always be scheduld again
 * by calling SchedulerReadyThread */
void SchedulerRemoveThread(MCoreThread_t *Thread)
{
	/* Variables */
	ListNode_t *ThreadNode = NULL;
	DataKey_t iKey;

	/* Get thread node if exists */
	iKey.Value = (int)Thread->ThreadId;
	ThreadNode = ListGetNodeByKey(GlbSchedulers[Thread->CpuId]->Threads, iKey, 0);

	/* Sanity */
	assert(ThreadNode != NULL);

	/* Disarm the thread */
	SchedulerDisarmThread(Thread);

	/* Remove the node */
	ListRemoveByNode(GlbSchedulers[Thread->CpuId]->Threads, ThreadNode);
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
	ListNode_t *sNode = NULL;

	/* Sanity */
	if (IoQueue == NULL
		|| GlbSchedulerEnabled != 1)
		return;

	/* Loop */
	_foreach_nolink(sNode, IoQueue)
	{
		/* Cast */
		MCoreThread_t *mThread = (MCoreThread_t*)sNode->Data;

		/* Sanity */
		if (mThread->Sleep != 0) 
		{
			/* Reduce */
			mThread->Sleep -= MIN(Ms, mThread->Sleep);

			/* Time to wakeup? */
			if (mThread->Sleep == 0)
			{
				/* Temporary pointer */
				ListNode_t *WakeNode = sNode;

				/* Skip to next node */
				sNode = sNode->Link;

				/* Remove this node */
				ListRemoveByNode(IoQueue, WakeNode);

				/* Store node */
				ListAppend(GlbSchedulerNodeRecycler, WakeNode);

				/* Grant it top priority */
				mThread->Queue = -1;
				mThread->SleepResource = NULL;

				/* Rearm thread */
				SchedulerReadyThread(mThread);
			}
			else {
				/* Go to next */
				sNode = sNode->Link;
			}
		}
		else {
			/* Go to next */
			sNode = sNode->Link;
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
	DataKey_t iKey;
	DataKey_t sKey;

	/* Mark current thread for sleep */
	CurrentThread = ThreadingGetCurrentThread(Cpu);

	/* Mark for sleep */
	CurrentThread->Flags |= THREADING_ENTER_SLEEP;

	/* Disarm the thread */
	SchedulerDisarmThread(CurrentThread);

	/* Setup Keys */
	iKey.Value = (int)CurrentThread->ThreadId;
	sKey.Value = (int)CurrentThread->Priority;

	/* Add to list */
	CurrentThread->SleepResource = Resource;
	CurrentThread->Sleep = Timeout;
	ListAppend(IoQueue, SchedulerGetNode(iKey, sKey, CurrentThread));

	/* Restore interrupts */
	InterruptRestoreState(IntrState);
}

/* These two functions either wakes one or all threads
 * waiting for a resource. */
int SchedulerWakeupOneThread(Addr_t *Resource)
{
	/* Find first thread matching resource */
	ListNode_t *SleepMatch = NULL;

	/* Sanity */
	if (IoQueue == NULL
		|| GlbSchedulerEnabled != 1)
		return 0;

	/* Loop */
	foreach(i, IoQueue)
	{
		/* Cast */
		MCoreThread_t *mThread = (MCoreThread_t*)i->Data;

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
		MCoreThread_t *mThread = (MCoreThread_t*)SleepMatch->Data;
		
		/* Cleanup */
		ListRemoveByNode(IoQueue, SleepMatch);
		ListAppend(GlbSchedulerNodeRecycler, SleepMatch);

		/* Grant it top priority */
		mThread->Queue = -1;
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
	ListNode_t *NextThreadNode = NULL;
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
			&& Thread->Queue < MCORE_SYSTEM_QUEUE) /* Must be below 60, 60 is highest normal queue */
		{
			/* Reduce priority */
			Thread->Queue++;

			/* Recalculate time-slice */
			Thread->TimeSlice = (Thread->Queue * 2) + MCORE_INITIAL_TIMESLICE;
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
		if (GlbSchedulers[Cpu]->Queues[i]->Length > 0)
		{
			/* FIFO algorithm in queues */
			NextThreadNode = ListPopFront(GlbSchedulers[Cpu]->Queues[i]);

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
		ListAppend(GlbSchedulerNodeRecycler, NextThreadNode);

		/* Return */
		return (MCoreThread_t*)NextThreadNode->Data;
	}
}