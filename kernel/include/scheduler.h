/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Multilevel Feedback Scheduler
 *  - Implements scheduling of threads by having a specified number of queues
 *    where each queue has a different timeslice, the longer a thread is running
 *    the less priority it gets, however longer timeslices it gets.
 */

#ifndef __VALI_SCHEDULER_H__
#define __VALI_SCHEDULER_H__

#include <os/osdefs.h>
#include <spinlock.h>
#include <time.h>

typedef struct list list_t;

/* Scheduler Definitions
 * Contains magic constants, bit definitions and settings. */
#define SCHEDULER_LEVEL_LOW             59
#define SCHEDULER_LEVEL_CRITICAL        60
#define SCHEDULER_LEVEL_COUNT           61

// Boosts happen every 5 seconds to prevent starvation in the scheduler
// Timeslices go from initial => initial + (SCHEDULER_TIMESLICE_STEP * SCHEDULER_LEVEL_COUNT)
#define SCHEDULER_TIMESLICE_INITIAL (10 * NSEC_PER_MSEC)
#define SCHEDULER_TIMESLICE_STEP    (2  * NSEC_PER_MSEC)
#define SCHEDULER_BOOST_MS          5000

#define SCHEDULER_FLAG_BOUND            0x1

typedef struct SchedulerObject SchedulerObject_t;

// Low overhead queues that are used by the scheduler, only in
// it's own core context, so no per list locking needed
typedef struct SchedulerQueue {
    SchedulerObject_t* Head;
    SchedulerObject_t* Tail;
} SchedulerQueue_t;

typedef struct Scheduler {
    int                    Enabled;
    clock_t          LastBoost;
    Spinlock_t       SyncObject;
    SchedulerQueue_t SleepQueue;
    SchedulerQueue_t       Queues[SCHEDULER_LEVEL_COUNT];

    // TODO bandwidth should be 64 bit, remove use of atomic here and protect
    // with a lock insteasd
    _Atomic(int)           ObjectCount;
    _Atomic(unsigned long) Bandwidth;
} Scheduler_t;

/* SchedulerCreateObject
 * Creates a new scheduling object and allocates a cpu core for the object.
 * This must be done before the kernel scheduler is used for the thread. */
KERNELAPI SchedulerObject_t* KERNELABI
SchedulerCreateObject(
    _In_ void*        payload,
    _In_ unsigned int flags);

/* SchedulerDestroyObject
 * Cleans up the resources associated with the kernel scheduler. */
KERNELAPI void KERNELABI
SchedulerDestroyObject(
    _In_ SchedulerObject_t* object);

/* SchedulerQueueObject
 * Queues up a new object for execution, at the next available timeslot. */
KERNELAPI oserr_t KERNELABI
SchedulerQueueObject(
    _In_ SchedulerObject_t* object);

/* SchedulerExpediteObject
 * If the given object is currently blocked, it will be unblocked and requeued
 * immediately. This function is core-safe and can be called across cores. */
KERNELAPI void KERNELABI
SchedulerExpediteObject(
    _In_ SchedulerObject_t* object);

/**
 * @brief Blocks the currently running thread. Can return different
 * sleep-state results. SCHEDULER_SLEEP_OK or SCHEDULER_SLEEP_INTERRUPTED.
 *
 * @param[In]  nanoseconds   The minimum number of nanoseconds to sleep for.
 * @param[Out] interruptedAt The timestamp the thread were awakened at if return is SCHEDULER_SLEEP_INTERRUPTED.
 * @return     Returns SCHEDULER_SLEEP_INTERRUPTED if a full sleep was not done, otherwise SCHEDULER_SLEEP_OK.
 */
KERNELAPI oserr_t KERNELABI
SchedulerSleep(
    _In_  clock_t  nanoseconds,
    _Out_ clock_t* interruptedAt);

/**
 * @brief Blocks the current scheduler object, and adds it to the given blocking queue.
 *
 * @param[In] blockQueue The block queue the current scheduler object should be queued at.
 * @param[In] deadline   The deadline for which to wake up the thread.
 */
KERNELAPI void KERNELABI
SchedulerBlock(
    _In_ list_t*        blockQueue,
    _In_ OSTimestamp_t* deadline);

/**
 * @brief Returns the last timeout reason for the current thread.
 *
 * @return Status for the last sleep.
 */
KERNELAPI oserr_t KERNELABI
SchedulerGetTimeoutReason(void);

/**
 * @brief The primary ticker function for the scheduler. Will return the next object available
 * for scheduling, and when the next tick should occur in the deadline parameter.
 *
 * @param[In]  object            The scheduler object that should be rescheduled.
 * @param[In]  preemptive        Non-zero if this was due to preemptive scheduling.
 * @param[In]  nanosecondsPassed The nanoseconds passed since last scheduling.
 * @param[Out] nextDeadlineOut   The nanoseconds untill next scheduling.
 * @return     The next payload that should be executed.
 */
KERNELAPI void* KERNELABI
SchedulerAdvance(
    _In_  SchedulerObject_t* object,
    _In_  int                preemptive,
    _In_  clock_t            nanosecondsPassed,
    _Out_ clock_t*           nextDeadlineOut);

/**
 * @brief Gets the current queue priority of the object.
 *
 * @param[In] object The object to read the queue of.
 * @return    The queue of the object.
 */
KERNELAPI int KERNELABI
SchedulerObjectGetQueue(
    _In_ SchedulerObject_t* object);

/**
 * @brief Gets the current cpu core affinity for the object.
 *
 * @param[In] object The object to read the affinity of.
 * @return    The cpu core id of the object.
 */
KERNELAPI uuid_t KERNELABI
SchedulerObjectGetAffinity(
    _In_ SchedulerObject_t* object);

/**
 * @brief Disables scheduling for the current core. This can be used in cases where we want to
 * schedule a number of threads without being interrupted before the end.
 */
KERNELAPI void KERNELABI
SchedulerDisable(void);

/**
 * @brief Enables scheduling for the current core. This immediately yields the current thread if
 * the current thread is marked as an idle thread.
 */
KERNELAPI void KERNELABI
SchedulerEnable(void);

#endif // !__VALI_SCHEDULER_H__
