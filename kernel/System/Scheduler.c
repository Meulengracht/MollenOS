/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
#include <List.h>
#include <Heap.h>
#include <stdio.h>

/* Globals */
Scheduler_t *GlbSchedulers[MCORE_MAX_SCHEDULERS];
list_t *SleepQueue = NULL;
volatile uint32_t GlbSchedulerEnabled = 0;

/* Init */
void SchedulerInit(Cpu_t cpu)
{
	int i;
	Scheduler_t *scheduler;

	/* Is this BSP setting up? */
	if (cpu == 0)
	{
		/* Null out stuff */
		for (i = 0; i < MCORE_MAX_SCHEDULERS; i++)
			GlbSchedulers[i] = NULL;

		/* Allocate Sleep */
		SleepQueue = list_create(LIST_SAFE);
	}

	/* Allocate a scheduler */
	scheduler = (Scheduler_t*)kmalloc(sizeof(Scheduler_t));

	/* Initialize all queues (Lock-Less) */
	for (i = 0; i < MCORE_SCHEDULER_LEVELS; i++)
		scheduler->Queues[i] = list_create(LIST_NORMAL);

	/* Reset boost timer */
	scheduler->BoostTimer = 0;
	scheduler->NumThreads = 0;

	/* Reset lock */
	SpinlockReset(&scheduler->Lock);

	/* Enable */
	GlbSchedulers[cpu] = scheduler;
	GlbSchedulerEnabled = 1;
}

/* Boost ALL threads to priority queue 0 */
void SchedulerBoost(Scheduler_t *Scheduler)
{
	int i = 0;
	list_node_t *node;
	MCoreThread_t *mThread;
	
	/* Step 1. Loop through all queues, pop their elements and append them to queue 0
	 * Reset time-slices */
	for (i = 1; i < MCORE_SCHEDULER_LEVELS; i++)
	{
		if (Scheduler->Queues[i]->length > 0)
		{
			node = list_pop_front(Scheduler->Queues[i]);

			while (node != NULL)
			{
				/* Reset timeslice */
				mThread = (MCoreThread_t*)node->data;
				mThread->TimeSlice = MCORE_INITIAL_TIMESLICE;
				mThread->Priority = 0;

				list_append(Scheduler->Queues[0], node);
				node = list_pop_front(Scheduler->Queues[i]);
			}
		}
	}
}

/* Add a thread */
void SchedulerReadyThread(list_node_t *Node)
{
	/* Add task to a queue based on its priority */
	MCoreThread_t *mThread = (MCoreThread_t*)Node->data;
	Cpu_t index = 0;
	uint32_t i = 0;
	
	/* Step 1. New thread? :) */
	if (mThread->Priority == -1)
	{
		/* Reduce priority */
		mThread->Priority = 0;

		/* Recalculate time-slice */
		mThread->TimeSlice = MCORE_INITIAL_TIMESLICE;
	}

	/* Step 2. Find the least used CPU */
	if (mThread->CpuId == 0xFF)
	{
		/* Yea, broadcast thread 
		 * Locate the least used CPU 
		 * TODO */
		while (GlbSchedulers[i] != NULL)
		{
			if (GlbSchedulers[i]->NumThreads < GlbSchedulers[index]->NumThreads)
				index = i;

			i++;
		}

		/* Now lock the cpu at that core for now */
		mThread->CpuId = index;
	}
	else
	{
		/* Add it to appropriate list */
		index = mThread->CpuId;
	}

	/* Get lock */
	SpinlockAcquire(&GlbSchedulers[index]->Lock);

	/* Append */
	list_append(GlbSchedulers[index]->Queues[mThread->Priority], Node);
	GlbSchedulers[index]->NumThreads++;

	/* Release lock */
	SpinlockRelease(&GlbSchedulers[index]->Lock);

	/* Wakeup CPU if sleeping */
	if (ThreadingIsCurrentTaskIdle(mThread->CpuId) != 0)
		ThreadingWakeCpu(mThread->CpuId);
}

/* Make a thread enter sleep */
void SchedulerSleepThread(Addr_t *Resource)
{
	/* Disable Interrupts 
	 * This is a fragile operation */
	IntStatus_t IntrState = InterruptDisable();

	/* Mark current thread for sleep and get its queue_node */
	list_node_t *tNode = ThreadingEnterSleep();

	/* Cast, we need the thread */
	MCoreThread_t *mThread = (MCoreThread_t*)tNode->data;

	/* Add to list */
	mThread->SleepResource = Resource;
	list_append(SleepQueue, tNode);

	/* Restore interrupts */
	InterruptRestoreState(IntrState);
}

/* Wake up a thread sleeping */
int SchedulerWakeupOneThread(Addr_t *Resource)
{
	/* Find first thread matching resource */
	list_node_t *match = NULL;
	foreach(i, SleepQueue)
	{
		MCoreThread_t *mThread = (MCoreThread_t*)i->data;

		if (mThread->SleepResource == Resource)
		{
			match = i;
			break;
		}
	}

	if (match != NULL)
	{
		MCoreThread_t *mThread = (MCoreThread_t*)match->data;
		list_remove_by_node(SleepQueue, match);

		/* Grant it top priority */
		mThread->Priority = -1;

		SchedulerReadyThread(match);
		return 1;
	}
	else
		return 0;
}

/* Wake up a all threads sleeping */
void SchedulerWakeupAllThreads(Addr_t *Resource)
{
	while (1)
	{
		if (!SchedulerWakeupOneThread(Resource))
			break;
	}
}

/* Schedule */
list_node_t *SchedulerGetNextTask(Cpu_t cpu, list_node_t *Node, int PreEmptive)
{
	int i;
	list_node_t *next = NULL;
	MCoreThread_t *mThread;
	uint32_t time_slice;

	/* Sanity */
	if (GlbSchedulerEnabled == 0 || GlbSchedulers[cpu] == NULL)
		return Node;

	/* Get the time delta */
	if (Node != NULL)
	{
		mThread = (MCoreThread_t*)Node->data;
		time_slice = mThread->TimeSlice;

		/* Increase its priority */
		if (PreEmptive != 0
			&& mThread->Priority < MCORE_SYSTEM_QUEUE) /* Must be below 60, 60 is highest normal queue */
		{
			/* Reduce priority */
			mThread->Priority++;

			/* Recalculate time-slice */
			mThread->TimeSlice = (mThread->Priority * 2) + MCORE_INITIAL_TIMESLICE;
		}

		/* Schedúle */
		SchedulerReadyThread(Node);
	}
	else
		time_slice = MCORE_INITIAL_TIMESLICE;

	/* Acquire Lock */
	SpinlockAcquireNoInt(&GlbSchedulers[cpu]->Lock);

	/* Step 3. Boost??? */
	GlbSchedulers[cpu]->BoostTimer += time_slice;

	if (GlbSchedulers[cpu]->BoostTimer >= MCORE_SCHEDULER_BOOST_MS)
	{
		SchedulerBoost(GlbSchedulers[cpu]);
		GlbSchedulers[cpu]->BoostTimer = 0;
	}

	/* Step 4. Get next node */
	for (i = 0; i < MCORE_SCHEDULER_LEVELS; i++)
	{
		/* Do we even have any nodes in here ? */
		if (GlbSchedulers[cpu]->Queues[i]->length > 0)
		{
			next = list_pop_front(GlbSchedulers[cpu]->Queues[i]);

			if (next != NULL)
			{
				GlbSchedulers[cpu]->NumThreads--;
				goto done;
			}
		}
	}

	/* Done */
done:

	/* Release Lock */
	SpinlockReleaseNoInt(&GlbSchedulers[cpu]->Lock);

	/* Return */
	return next;
}