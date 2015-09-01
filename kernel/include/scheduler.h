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
#include <Arch.h>
#include <crtdefs.h>
#include <List.h>

/* Definitions */
#define MCORE_MAX_SCHEDULERS		64
#define MCORE_SYSTEM_QUEUE			60
#define MCORE_SCHEDULER_LEVELS		61
#define MCORE_INITIAL_TIMESLICE		10
#define MCORE_IDLE_TIMESLICE		300
#define MCORE_SCHEDULER_BOOST_MS	2000

/* Structures */
typedef struct _MCoreScheduler
{
	/* Queues */
	list_t *Queues[MCORE_SCHEDULER_LEVELS];

	/* Boost Timer */
	volatile uint32_t BoostTimer;

	/* Number of threads */
	volatile uint32_t NumThreads;

	/* Lock */
	Spinlock_t Lock;

} Scheduler_t;

/* Prototypes */
_CRT_EXTERN void SchedulerInit(Cpu_t cpu);
_CRT_EXTERN void SchedulerReadyThread(list_node_t *Node);
_CRT_EXTERN list_node_t *SchedulerGetNextTask(Cpu_t cpu, list_node_t *Node, int PreEmptive);

_CRT_EXTERN void SchedulerSleepThread(Addr_t *Resource);
_CRT_EXTERN int SchedulerWakeupOneThread(Addr_t *Resource);
_CRT_EXTERN void SchedulerWakeupAllThreads(Addr_t *Resource);

#endif // !_MCORE_SCHEDULER_H_
