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
* Priorities 60 - 0 start at 10 ms, slowly increases to 300 ms.
* Priority boosts every 1000 ms? 
* On yields, keep priority.
* On task-switchs, decrease priority.
* A thread can only stay a maximum in each priority.
*/

/* Includes */
#include <arch.h>
#include <scheduler.h>
#include <list.h>
#include <stdio.h>

/* Globals */
list_t *thread_queues[MCORE_SCHEDULER_LEVELS];
volatile uint32_t glb_boost_timer = 0;
volatile uint32_t glb_scheduler_enabled = 0;

/* Init */
void scheduler_init(void)
{
	int i;

	/* Initialize all queues */
	for (i = 0; i < MCORE_SCHEDULER_LEVELS; i++)
		thread_queues[i] = list_create(LIST_SAFE);
	
	/* Reset boost timer */
	glb_boost_timer = 0;

	/* Enable */
	glb_scheduler_enabled = 1;
}

/* Boost ALL threads to priority queue 0 */
void scheduler_boost(void)
{
	int i = 0;
	list_node_t *node;
	thread_t *t;

	/* Step 1. Loop through all queues, pop their elements and append them to queue 0
	 * Reset time-slices */
	for (i = 1; i < (MCORE_SCHEDULER_LEVELS - 1); i++)
	{
		if (thread_queues[i]->length > 0)
		{
			node = list_pop(thread_queues[i]);

			while (node != NULL)
			{
				/* Reset timeslice */
				t = (thread_t*)node->data;
				t->time_slice = MCORE_INITIAL_TIMESLICE;
				t->priority = 0;

				list_append(thread_queues[0], node);
				node = list_pop(thread_queues[i]);
			}
		}
	}
}

/* Add a thread */
void scheduler_ready_thread(list_node_t *node)
{
	/* Add task to a queue based on its priority */
	thread_t *t = (thread_t*)node->data;
	
	/* Step 1. Is this a YIELD?!?!? */
	if (t->priority == -1)
	{
		/* Reduce priority */
		t->priority++;

		/* Recalculate time-slice */
		t->time_slice = MCORE_INITIAL_TIMESLICE;
	}

	/* Step 2. Add it to appropriate list */
	list_append(thread_queues[t->priority], node);
}

/* Schedule */
list_node_t *scheduler_schedule(cpu_t cpu, list_node_t *node, int preemptive)
{
	int i;
	list_node_t *next = NULL;
	thread_t *t;
	uint32_t time_slice;

	/* Sanity */
	if (glb_scheduler_enabled == 0)
		return node;

	/* Add task to a queue based on its priority */
	if (node != NULL)
	{
		t = (thread_t*)node->data;
		time_slice = t->time_slice;

		/* Step 1. Is this a YIELD?!?!? */
		if (preemptive != 0
			&& t->priority < (MCORE_SCHEDULER_LEVELS - 2))
		{
			/* Reduce priority */
			t->priority++;

			/* Recalculate time-slice */
			t->time_slice = (t->priority * 5) + MCORE_INITIAL_TIMESLICE;
		}

		/* Step 2. Add it to appropriate list */
		list_append(thread_queues[t->priority], node);
	}
	else
		time_slice = MCORE_INITIAL_TIMESLICE;

	/* Step 3. Boost??? */
	if (cpu == 0)
	{
		glb_boost_timer += time_slice;

		if (glb_boost_timer >= MCORE_SCHEDULER_BOOST_MS)
		{
			scheduler_boost();

			glb_boost_timer = 0;
		}
	}

	/* Get next node */

	/* Step 4. Always do system nodes first */
	if (thread_queues[MCORE_SCHEDULER_LEVELS - 1]->length > 0)
	{
		next = list_pop(thread_queues[MCORE_SCHEDULER_LEVELS - 1]);

		if (next != NULL)
			return next;
	}

	/* Step 5. Then normal */
	for (i = 0; i < (MCORE_SCHEDULER_LEVELS - 1); i++)
	{
		if (thread_queues[i]->length > 0)
		{
			next = list_pop(thread_queues[i]);

			if (next != NULL)
				break;
		}
	}

	return next;
}