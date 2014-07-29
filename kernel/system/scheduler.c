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
#include <arch.h>
#include <scheduler.h>
#include <list.h>
#include <heap.h>
#include <stdio.h>

/* Globals */
scheduler_t *glb_schedulers[64];
list_t *sleep_queue = NULL;
volatile uint32_t glb_scheduler_enabled = 0;

/* Init */
void scheduler_init(cpu_t cpu)
{
	int i;
	scheduler_t *scheduler;

	/* Is this BSP setting up? */
	if (cpu == 0)
	{
		/* Null out stuff */
		for (i = 0; i < 64; i++)
			glb_schedulers[i] = NULL;

		/* Allocate Sleep */
		sleep_queue = list_create(LIST_SAFE);
	}

	/* Allocate a scheduler */
	scheduler = (scheduler_t*)kmalloc(sizeof(scheduler_t));

	/* Initialize all queues (Lock-Less) */
	for (i = 0; i < MCORE_SCHEDULER_LEVELS; i++)
		scheduler->queues[i] = list_create(LIST_NORMAL);

	/* Reset boost timer */
	scheduler->boost_timer = 0;
	scheduler->num_threads = 0;

	/* Reset lock */
	spinlock_reset(&scheduler->lock);

	/* Enable */
	glb_schedulers[cpu] = scheduler;
	glb_scheduler_enabled = 1;
}

/* Boost ALL threads to priority queue 0 */
void scheduler_boost(scheduler_t *scheduler)
{
	int i = 0;
	list_node_t *node;
	thread_t *t;

	/* Step 1. Loop through all queues, pop their elements and append them to queue 0
	 * Reset time-slices */
	for (i = 1; i < MCORE_SYSTEM_QUEUE; i++)
	{
		if (scheduler->queues[i]->length > 0)
		{
			node = list_pop_front(scheduler->queues[i]);

			while (node != NULL)
			{
				/* Reset timeslice */
				t = (thread_t*)node->data;
				t->time_slice = MCORE_INITIAL_TIMESLICE;
				t->priority = 0;

				list_append(scheduler->queues[0], node);
				node = list_pop_front(scheduler->queues[i]);
			}
		}
	}
}

/* Add a thread */
void scheduler_ready_thread(list_node_t *node)
{
	/* Add task to a queue based on its priority */
	thread_t *t = (thread_t*)node->data;
	cpu_t index = 0;
	uint32_t i = 0;
	uint32_t lowest = 0;
	
	/* Step 1. New thread? :) */
	if (t->priority == -1)
	{
		/* Reduce priority */
		t->priority = 0;

		/* Recalculate time-slice */
		t->time_slice = MCORE_INITIAL_TIMESLICE;
	}

	/* Step 2. Find the least used CPU */
	if (t->cpu_id == 0xFF)
	{
		/* Yea, broadcast thread 
		 * Locate the least used CPU 
		 * TODO */
		while (glb_schedulers[i] != NULL)
		{
			if (glb_schedulers[i]->num_threads < lowest)
				index = i;

			i++;
		}

		/* Now lock the cpu at that core for now */
		t->cpu_id = index;
	}
	else
	{
		/* Add it to appropriate list */
		index = t->cpu_id;
	}

	/* Get lock */
	spinlock_acquire(&glb_schedulers[index]->lock);

	/* Append */
	list_append(glb_schedulers[index]->queues[t->priority], node);
	glb_schedulers[index]->num_threads++;

	/* Release lock */
	spinlock_release(&glb_schedulers[index]->lock);
}

/* Make a thread enter sleep */
void scheduler_sleep_thread(addr_t *resource)
{
	/* Mark current thread for sleep and get its queue_node */
	interrupt_status_t int_state = interrupt_disable();
	list_node_t *t_node = threading_enter_sleep();
	thread_t *t = (thread_t*)t_node->data;

	/* Add to list */
	t->sleep_resource = resource;
	list_append(sleep_queue, t_node);

	/* Restore interrupts */
	interrupt_set_state(int_state);
}

/* Wake up a thread sleeping */
int scheduler_wakeup_one(addr_t *resource)
{
	/* Find first thread matching resource */
	list_node_t *match = NULL;
	foreach(i, sleep_queue)
	{
		thread_t *t = (thread_t*)i->data;

		if (t->sleep_resource == resource)
		{
			match = i;
			break;
		}
	}

	if (match != NULL)
	{
		thread_t *t = (thread_t*)match->data;
		list_remove_by_node(sleep_queue, match);

		/* Grant it top priority */
		t->priority = -1;

		scheduler_ready_thread(match);
		return 1;
	}
	else
		return 0;
}

/* Wake up a all threads sleeping */
void scheduler_wakeup_all(addr_t *resource)
{
	while (1)
	{
		if (!scheduler_wakeup_one(resource))
			break;
	}
}

/* Schedule */
list_node_t *scheduler_schedule(cpu_t cpu, list_node_t *node, int preemptive)
{
	int i;
	list_node_t *next = NULL;
	thread_t *t;
	uint32_t time_slice;

	/* Sanity */
	if (glb_scheduler_enabled == 0 || glb_schedulers[cpu] == NULL)
		return node;

	/* Add task to a queue based on its priority */
	if (node != NULL)
	{
		t = (thread_t*)node->data;
		time_slice = t->time_slice;

		/* Step 1. Is this a YIELD?!?!? */
		if (preemptive != 0
			&& t->priority < (MCORE_SYSTEM_QUEUE - 1)) /* Must be below 59, 59 is highest normal queue */
		{
			/* Reduce priority */
			t->priority++;

			/* Recalculate time-slice */
			t->time_slice = (t->priority * 2) + MCORE_INITIAL_TIMESLICE;
		}

		/* Step 2. Add it to appropriate list */
		scheduler_ready_thread(node);
	}
	else
		time_slice = MCORE_INITIAL_TIMESLICE;

	/* Acquire Lock */
	spinlock_acquire(&glb_schedulers[cpu]->lock);

	/* Step 3. Boost??? */
	glb_schedulers[cpu]->boost_timer += time_slice;

	if (glb_schedulers[cpu]->boost_timer >= MCORE_SCHEDULER_BOOST_MS)
	{
		scheduler_boost(glb_schedulers[cpu]);

		glb_schedulers[cpu]->boost_timer = 0;
	}

	/* Get next node */

	/* Step 4. Always do system nodes first */
	if (glb_schedulers[cpu]->queues[MCORE_SYSTEM_QUEUE]->length > 0)
	{
		next = list_pop_front(glb_schedulers[cpu]->queues[MCORE_SYSTEM_QUEUE]);

		if (next != NULL)
		{
			glb_schedulers[cpu]->num_threads--;
			goto done;
		}
	}

	/* Step 5. Then normal */
	for (i = 0; i < MCORE_SYSTEM_QUEUE; i++)
	{
		if (glb_schedulers[cpu]->queues[i]->length > 0)
		{
			next = list_pop_front(glb_schedulers[cpu]->queues[i]);

			if (next != NULL)
			{
				glb_schedulers[cpu]->num_threads--;
				goto done;
			}
				
		}
	}

	/* Done */
done:

	/* Release Lock */
	spinlock_release(&glb_schedulers[cpu]->lock);

	/* Return */
	return next;
}