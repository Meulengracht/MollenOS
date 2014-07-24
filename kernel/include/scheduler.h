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

#ifndef _MCORE_SCHEDULER_H_
#define _MCORE_SCHEDULER_H_

/* Includes */
#include <arch.h>
#include <crtdefs.h>
#include <list.h>

/* Definitions */
#define MCORE_SYSTEM_QUEUE			60
#define MCORE_SCHEDULER_LEVELS		61
#define MCORE_INITIAL_TIMESLICE		10
#define MCORE_IDLE_TIMESLICE		300
#define MCORE_SCHEDULER_BOOST_MS	2000

/* Structures */
typedef struct _mcore_scheduler
{
	/* Queues */
	list_t *queues[MCORE_SCHEDULER_LEVELS];

	/* Boost Timer */
	volatile uint32_t boost_timer;

	/* Number of threads */
	volatile uint32_t num_threads;

	/* Lock */
	spinlock_t lock;

} scheduler_t;

/* Prototypes */
_CRT_EXTERN void scheduler_init(cpu_t cpu);
_CRT_EXTERN void scheduler_ready_thread(list_node_t* node);
_CRT_EXTERN list_node_t *scheduler_schedule(cpu_t cpu, list_node_t *node, int preemptive);

_CRT_EXTERN void scheduler_sleep_thread(addr_t *resource);
_CRT_EXTERN int scheduler_wakeup_one(addr_t *resource);
_CRT_EXTERN void scheduler_wakeup_all(addr_t *resource);

#endif // !_MCORE_SCHEDULER_H_
