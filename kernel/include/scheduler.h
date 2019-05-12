/* MollenOS
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
#include <ds/ds.h>
#include <ds/collection.h>
#include <time.h>

// Forward declarations
typedef struct _SchedulerLockedQueue SchedulerLockedQueue_t;

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

typedef enum {
    SchedulerObjectStateIdle,
    SchedulerObjectStateQueued,
    SchedulerObjectStateBlocked,
    SchedulerObjectStateRunning,
    SchedulerObjectStateZombie
} SchedulerObjectState_t;

typedef struct _SchedulerObject {
    CollectionItem_t                Header;
    volatile SchedulerObjectState_t State;
    volatile Flags_t                Flags;
    UUId_t                          CoreId;
    size_t                          TimeSlice;
    int                             Queue;
    struct _SchedulerObject*        Link;
    void*                           Object;
    
    SchedulerLockedQueue_t*         QueueHandle;
    int                             Timeout;
    size_t                          TimeLeft;
    clock_t                         InterruptedAt;
} SchedulerObject_t;

// Queues that are synchronized with the scheduler, and can be used
// as generic blocking queus
typedef struct _SchedulerLockedQueue {
    CollectionItem_t Header;
    SafeMemoryLock_t SyncObject;
    Collection_t     Queue;
} SchedulerLockedQueue_t;

// Low overhead queues that are used by the scheduler, only in
// it's own core context, so no per list locking needed
typedef struct {
    SchedulerObject_t* Head;
    SchedulerObject_t* Tail;
} SchedulerQueue_t;

typedef struct {
    SafeMemoryLock_t       SyncObject;
    SchedulerQueue_t       SleepQueue;
    SchedulerQueue_t       Queues[SCHEDULER_LEVEL_COUNT];
    _Atomic(int)           ObjectCount;
    _Atomic(unsigned long) Bandwidth;
    clock_t                LastBoost;
} SystemScheduler_t;

#define SCHEDULER_LOCKED_QUEUE_INIT { COLLECTION_NODE_INIT(0), { 0 }, COLLECTION_INIT(KeyId) }
#define SCHEDULER_INIT              { { 0 }, { 0 }, { { 0 } }, ATOMIC_VAR_INIT(0), ATOMIC_VAR_INIT(0), 0 }

/* SchedulerLockedQueueConstruct
 * Initializes a new locked queue. */
KERNELAPI void KERNELABI
SchedulerLockedQueueConstruct(
    _In_ SchedulerLockedQueue_t* Queue);

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
KERNELAPI void KERNELABI
SchedulerQueueObject(
    _In_ SchedulerObject_t* Object);

/* SchedulerExpediteObject
 * If a scheduler object is in a blocked state, this will force un-block it and
 * allow it to run again. */
KERNELAPI void KERNELABI
SchedulerExpediteObject(
    _In_ SchedulerObject_t* Object);

/* SchedulerSleep
 * Blocks the currently running thread for <milliseconds>. Can return different
 * sleep-state results. SCHEDULER_SLEEP_OK or SCHEDULER_SLEEP_INTERRUPTED. */
KERNELAPI int KERNELABI
SchedulerSleep(
    _In_ size_t Milliseconds);

/* SchedulerBlock(Timed)
 * Blocks the current thread and adds it to the given block queue. This is atomically
 * protected operating, and the lock must be held before calling this function.
 * If a timeout is passed the queue must have been registed with SchedulerRegisterQueue, 
 * otherwise the timeout is not invoked. */
KERNELAPI OsStatus_t KERNELABI
SchedulerBlock(
    _In_ SchedulerLockedQueue_t* Queue,
    _In_ size_t                  Timeout);

/* SchedulerUnblock
 * Unblocks an thread from the given queue. The lock must be held while calling this
 * function. */
KERNELAPI void KERNELABI
SchedulerUnblock(
    _In_ SchedulerLockedQueue_t* Queue);

/* SchedulerAdvance 
 * This should be called by the underlying archteicture code
 * to get the next thread that is to be run. */
KERNELAPI void* KERNELABI
SchedulerAdvance(
    _In_  SchedulerObject_t* Object,
    _In_  int                Preemptive,
    _In_  size_t             MillisecondsPassed,
    _Out_ size_t*            NextDeadlineOut);

#endif // !__VALI_SCHEDULER_H__
