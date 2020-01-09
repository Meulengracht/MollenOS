/**
 * MollenOS
 *
 * Copyright 2016, Philip Meulengracht
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
 * Multilevel Feedback Scheduler
 *  - Implements scheduling of threads by having a specified number of queues
 *    where each queue has a different timeslice, the longer a thread is running
 *    the less priority it gets, however longer timeslices it gets.
 */

#ifndef __VALI_SCHEDULER_H__
#define __VALI_SCHEDULER_H__

#include <os/osdefs.h>
#include <os/spinlock.h>
#include <irq_spinlock.h>
#include <time.h>

typedef struct list list_t;

/* Scheduler Definitions
 * Contains magic constants, bit definitions and settings. */
#define SCHEDULER_LEVEL_LOW             59
#define SCHEDULER_LEVEL_CRITICAL        60
#define SCHEDULER_LEVEL_COUNT           61

// Boosts happen every 10 seconds to prevent starvation in the scheduler
// Timeslices go from initial => initial + (2 * SCHEDULER_LEVEL_COUNT)
#define SCHEDULER_TIMESLICE_INITIAL     10
#define SCHEDULER_BOOST                 10000

#define SCHEDULER_TIMEOUT_INFINITE      0
#define SCHEDULER_SLEEP_OK              0
#define SCHEDULER_SLEEP_INTERRUPTED     1

#define SCHEDULER_FLAG_BOUND            0x1

typedef struct SchedulerObject SchedulerObject_t;

// Low overhead queues that are used by the scheduler, only in
// it's own core context, so no per list locking needed
typedef struct SchedulerQueue {
    SchedulerObject_t* Head;
    SchedulerObject_t* Tail;
} SchedulerQueue_t;

typedef struct SystemScheduler {
    IrqSpinlock_t          SyncObject;
    SchedulerQueue_t       SleepQueue;
    SchedulerQueue_t       Queues[SCHEDULER_LEVEL_COUNT];
    _Atomic(int)           ObjectCount;
    _Atomic(unsigned long) Bandwidth;
    clock_t                LastBoost;
} SystemScheduler_t;

#define SCHEDULER_INIT { { 0 }, { 0 }, { { 0 } }, ATOMIC_VAR_INIT(0), ATOMIC_VAR_INIT(0), 0 }

/* SchedulerCreateObject
 * Creates a new scheduling object and allocates a cpu core for the object.
 * This must be done before the kernel scheduler is used for the thread. */
KERNELAPI SchedulerObject_t* KERNELABI
SchedulerCreateObject(
    _In_ void*   Payload,
    _In_ Flags_t Flags);

/* SchedulerDestroyObject
 * Cleans up the resources associated with the kernel scheduler. */
KERNELAPI void KERNELABI
SchedulerDestroyObject(
    _In_ SchedulerObject_t* Object);

/* SchedulerQueueObject
 * Queues up a new object for execution, at the next available timeslot. */
KERNELAPI OsStatus_t KERNELABI
SchedulerQueueObject(
    _In_ SchedulerObject_t* Object);

/* SchedulerExpediteObject
 * If the given object is currently blocked, it will be unblocked and requeued
 * immediately. This function is core-safe and can be called across cores. */
KERNELAPI void KERNELABI
SchedulerExpediteObject(
    _In_ SchedulerObject_t* Object);

/**
 * SchedulerUnblockObject
 * * If the given object is currently blocked it will be removed from any blocked
 * * queue and reset to running state. This can only be called on the same thread. 
 */
KERNELAPI void KERNELABI
SchedulerUnblockObject(
    _In_ SchedulerObject_t* Object);

/**
 * SchedulerSleep
 * * Blocks the currently running thread for @Milliseconds. Can return different
 * * sleep-state results. SCHEDULER_SLEEP_OK or SCHEDULER_SLEEP_INTERRUPTED. 
 */
KERNELAPI int KERNELABI
SchedulerSleep(
    _In_  size_t   Milliseconds,
    _Out_ clock_t* InterruptedAt);

/**
 * SchedulerBlock
 * * Blocks the current scheduler object, and adds it to the given blocking queue.
 * 
 */
KERNELAPI void KERNELABI
SchedulerBlock(
    _In_ list_t* BlockQueue,
    _In_ size_t  Timeout);

/**
 * SchedulerIsTimeout
 */
KERNELAPI int KERNELABI
SchedulerIsTimeout(void);

/* SchedulerAdvance 
 * This should be called by the underlying archteicture code
 * to get the next thread that is to be run. */
KERNELAPI void* KERNELABI
SchedulerAdvance(
    _In_  SchedulerObject_t* Object,
    _In_  int                Preemptive,
    _In_  size_t             MillisecondsPassed,
    _Out_ size_t*            NextDeadlineOut);

KERNELAPI int KERNELABI
SchedulerObjectGetQueue(
    _In_ SchedulerObject_t*);

KERNELAPI UUId_t KERNELABI
SchedulerObjectGetAffinity(
    _In_ SchedulerObject_t*);

#endif // !__VALI_SCHEDULER_H__
