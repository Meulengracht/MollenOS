/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
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

/* Includes 
 * - Library */
#include <os/osdefs.h>

/* Includes
 * - System */
#include <criticalsection.h>
#include <threading.h>

/* Scheduler Definitions
 * Contains magic constants, bit definitions and settings. */
#define SCHEDULER_LEVEL_LOW             59
#define SCHEDULER_LEVEL_CRITICAL        60
#define SCHEDULER_LEVEL_COUNT           61
#define SCHEDULER_TIMESLICE_INITIAL     10
#define SCHEDULER_BOOST                 3000

#define SCHEDULER_CPU_SELECT            0xFF
#define SCHEDULER_TIMEOUT_INFINITE      0
#define SCHEDULER_SLEEP_OK              0
#define SCHEDULER_SLEEP_TIMEOUT         1
#define SCHEDULER_SLEEP_INTERRUPTED     2

/* MCoreSchedulerQueue
 * Represents a queue level in the scheduler. */
typedef struct _MCoreSchedulerQueue {
    MCoreThread_t               *Head;
    MCoreThread_t               *Tail;
} SchedulerQueue_t;

/* MCoreScheduler
 * The core scheduler, contains information needed
 * to keep track of active threads and priority queues. */
typedef struct _MCoreScheduler {
    SchedulerQueue_t            Queues[SCHEDULER_LEVEL_COUNT];
    size_t                      BoostTimer;
    int                         ThreadCount;
    CriticalSection_t           QueueLock;
} Scheduler_t;

/* SchedulerInitialize
 * Initializes state variables and global static resources. */
KERNELAPI
void
KERNELABI
SchedulerInitialize(void);

/* SchedulerCreate
 * Creates and initializes a scheduler for the given cpu-id. */
KERNELAPI
void
KERNELABI
SchedulerCreate(
    _In_ UUId_t Cpu);

/* SchedulerThreadInitialize
 * Can be called by the creation of a new thread to initalize
 * all the scheduler data for that thread. */
KERNELAPI
void
KERNELABI
SchedulerThreadInitialize(
    _In_ MCoreThread_t *Thread,
    _In_ Flags_t Flags);

/* SchedulerThreadQueue
 * Queues up a thread for execution. */
KERNELAPI
OsStatus_t
KERNELABI
SchedulerThreadQueue(
    _In_ MCoreThread_t *Thread);

/* SchedulerThreadDequeue
 * Disarms a thread from all queues and mark the thread inactive. */
KERNELAPI
OsStatus_t
KERNELABI
SchedulerThreadDequeue(
    _In_ MCoreThread_t *Thread);

/* SchedulerThreadSleep
 * Enters the current thread into sleep-queue. Can return different
 * sleep-state results. SCHEDULER_SLEEP_OK or SCHEDULER_SLEEP_TIMEOUT. */
KERNELAPI
int
KERNELABI
SchedulerThreadSleep(
    _In_ uintptr_t*         Handle,
    _In_ size_t             Timeout);

/* SchedulerThreadSignal
 * Finds a sleeping thread with the given thread id and wakes it. */
KERNELAPI
OsStatus_t
KERNELABI
SchedulerThreadSignal(
    _In_ MCoreThread_t *Thread);

/* SchedulerHandleSignal
 * Finds a sleeping thread with the given sleep-handle and wakes it. */
KERNELAPI
OsStatus_t
KERNELABI
SchedulerHandleSignal(
    _In_ uintptr_t *Handle);

/* SchedulerHandleSignalAll
 * Finds any sleeping threads on the given handle and wakes them. */
KERNELAPI
void
KERNELABI
SchedulerHandleSignalAll(
    _In_ uintptr_t *Handle);

/* SchedulerTick
 * Iterates the io-queue and handle any threads that will timeout
 * on the tick. */
KERNELAPI
void
KERNELABI
SchedulerTick(
    _In_ size_t Milliseconds);

/* SchedulerThreadSchedule 
 * This should be called by the underlying archteicture code
 * to get the next thread that is to be run. */
KERNELAPI
MCoreThread_t*
KERNELABI
SchedulerThreadSchedule(
    _In_ UUId_t Cpu,
    _In_ MCoreThread_t *Thread,
    _In_ int Preemptive);

#endif // !_MCORE_SCHEDULER_H_
