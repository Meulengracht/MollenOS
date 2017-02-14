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
#include <arch.h>
#include <os/osdefs.h>
#include <ds/list.h>
#include <threading.h>
#include <criticalsection.h>

/* Definitions */
#define MCORE_SYSTEM_QUEUE			60
#define MCORE_SCHEDULER_LEVELS		61
#define MCORE_INITIAL_TIMESLICE		10
#define MCORE_IDLE_TIMESLICE		300
#define MCORE_SCHEDULER_BOOST_MS	2000

/* Structures */
typedef struct _MCoreScheduler {
	List_t					*Threads;
	List_t					*Queues[MCORE_SCHEDULER_LEVELS];
	size_t					BoostTimer;
	int						NumThreads;
	CriticalSection_t		Lock;
} Scheduler_t;

/* Prototypes */

/* This initializes the scheduler for the
 * given cpu_id, the first call to this
 * will also initialize the scheduler enviornment */
__EXTERN void SchedulerInit(Cpu_t Cpu);

/* This function arms a thread for scheduling
 * in most cases this is called with a prefilled
 * priority of -1 to make it run almost immediately */
__EXTERN void SchedulerReadyThread(MCoreThread_t *Thread);

/* This function is primarily used to remove a thread from
 * scheduling totally, but it can always be scheduld again
 * by calling SchedulerReadyThread */
__EXTERN void SchedulerRemoveThread(MCoreThread_t *Thread);

/* Schedule 
 * This should be called by the underlying archteicture code
 * to get the next thread that is to be run. */
__EXTERN MCoreThread_t *SchedulerGetNextTask(Cpu_t Cpu, MCoreThread_t *Thread, int PreEmptive);

/* This is used by timer code to reduce threads's timeout
 * if this function wasn't called then sleeping threads and 
 * waiting threads would never be armed again. */
__EXTERN void SchedulerApplyMs(size_t MilliSeconds);

/* This function sleeps the current thread either by resource,
 * by time, or both. If resource is NULL then it will wake the
 * thread after <timeout> ms. If infinite wait is required set
 * timeout to 0 */
_CRT_EXPORT void SchedulerSleepThread(Addr_t *Resource, size_t Timeout);

/* These two functions either wakes one or all threads
 * waiting for a resource. */
_CRT_EXPORT int SchedulerWakeupOneThread(Addr_t *Resource);
_CRT_EXPORT void SchedulerWakeupAllThreads(Addr_t *Resource);

#endif // !_MCORE_SCHEDULER_H_
