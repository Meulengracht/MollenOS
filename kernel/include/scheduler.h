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
#include <time.h>

typedef struct _MCoreThread MCoreThread_t;

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
#define SCHEDULER_SLEEP_TIMEOUT         1
#define SCHEDULER_SLEEP_INTERRUPTED     2
#define SCHEDULER_SLEEP_SYNC_FAILED     3

#define SCHEDULER_FLAG_BOUND            0x1
#define SCHEDULER_FLAG_BLOCK_IN_PRG     0x2

typedef struct {
    MCoreThread_t* Head;
    MCoreThread_t* Tail;
} SchedulerQueue_t;

typedef struct {
    SafeMemoryLock_t SyncObject;
    SchedulerQueue_t Queues[SCHEDULER_LEVEL_COUNT];
    atomic_int       ThreadCount;
    atomic_uint      Bandwidth;
    clock_t          LastBoost;
} SystemScheduler_t;

#define SCHEDULER_INIT { { 0 }, { { 0 } }, ATOMIC_VAR_INIT(0), ATOMIC_VAR_INIT(0), 0 }

/* SchedulerThreadInitialize
 * Initializes the thread for scheduling. This must be done before the kernel
 * scheduler is used for the thread. */
KERNELAPI void KERNELABI
SchedulerThreadInitialize(
    _In_ MCoreThread_t* Thread,
    _In_ Flags_t        Flags);
    
/* SchedulerThreadFinalize
 * Cleans up the resources associated with the kernel scheduler. */
KERNELAPI void KERNELABI
SchedulerThreadFinalize(
    _In_ MCoreThread_t* Thread);

/* SchedulerThreadQueue
 * Queues up a new thread for execution on the either least-loaded core, or the specified
 * core in the thread structure. */
KERNELAPI void KERNELABI
SchedulerThreadQueue(
    _In_ MCoreThread_t* Thread);

/* SchedulerThreadSleep
 * Enters the current thread into sleep-queue. Can return different
 * sleep-state results. SCHEDULER_SLEEP_OK or SCHEDULER_SLEEP_TIMEOUT. */
KERNELAPI int KERNELABI
SchedulerThreadSleep(
    _In_ uintptr_t* Handle,
    _In_ size_t     Timeout);

/* SchedulerAtomicThreadSleep
 * Enters the current thread into sleep-queue. This is done by using a synchronized
 * queueing by utilizing the atomic memory compares. If the value has changed before going
 * to sleep, it will return SCHEDULER_SLEEP_SYNC_FAILED. */
KERNELAPI int KERNELABI
SchedulerAtomicThreadSleep(
    _In_ atomic_int*        Object,
    _In_ int*               ExpectedValue,
    _In_ size_t             Timeout);

/* SchedulerThreadSignal
 * Finds a sleeping thread with the given thread id and wakes it. */
KERNELAPI OsStatus_t KERNELABI
SchedulerThreadSignal(
    _In_ MCoreThread_t*     Thread);

/* SchedulerHandleSignal
 * Finds a sleeping thread with the given sleep-handle and wakes it. */
KERNELAPI OsStatus_t KERNELABI
SchedulerHandleSignal(
    _In_ uintptr_t*         Handle);

/* SchedulerHandleSignalAll
 * Finds any sleeping threads on the given handle and wakes them. */
KERNELAPI void KERNELABI
SchedulerHandleSignalAll(
    _In_ uintptr_t*         Handle);

/* SchedulerTick
 * Iterates the io-queue and handle any threads that will timeout
 * on the tick. */
KERNELAPI void KERNELABI
SchedulerTick(
    _In_ size_t             Milliseconds);

/* SchedulerThreadSchedule 
 * This should be called by the underlying archteicture code
 * to get the next thread that is to be run. */
KERNELAPI MCoreThread_t* KERNELABI
SchedulerThreadSchedule(
    _In_ MCoreThread_t*     Thread,
    _In_ int                Preemptive);

#endif // !__VALI_SCHEDULER_H__
