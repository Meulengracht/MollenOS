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
* Priorities 60 - 0 start at 10 ms, slowly increases to 300 ms.
* Priority boosts every 1000 ms?
* On yields, keep priority.
* On task-switchs, decrease priority.
* A thread can only stay a maximum in each priority.
*/

#ifndef _MCORE_SCHEDULER_H_
#define _MCORE_SCHEDULER_H_

/* Includes */
#include <crtdefs.h>

#include <Arch.h>
#include <List.h>
#include <Threading.h>

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
	/* Thread List */
	list_t *Threads;

	/* Queues */
	list_t *Queues[MCORE_SCHEDULER_LEVELS];

	/* Boost Timer */
	size_t BoostTimer;

	/* Number of threads */
	int NumThreads;

	/* Lock */
	Spinlock_t Lock;

} Scheduler_t;

/* Prototypes */
_CRT_EXTERN void SchedulerInit(Cpu_t Cpu);

/* Ready a thread in the scheduler */
_CRT_EXTERN void SchedulerReadyThread(MCoreThread_t *Thread);

/* Remove a thread from scheduler */
_CRT_EXTERN void SchedulerRemoveThread(MCoreThread_t *Thread);

/* Schedule */
_CRT_EXTERN MCoreThread_t *SchedulerGetNextTask(Cpu_t Cpu, MCoreThread_t *Thread, int PreEmptive);

/* Sleep, Wake */
_CRT_EXPORT void SchedulerSleepThread(Addr_t *Resource);
_CRT_EXPORT int SchedulerWakeupOneThread(Addr_t *Resource);
_CRT_EXPORT void SchedulerWakeupAllThreads(Addr_t *Resource);

#endif // !_MCORE_SCHEDULER_H_
