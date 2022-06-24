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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Multilevel Feedback Scheduler
 *  - Implements scheduling of threads by having a specified number of queues
 *    where each queue has a different timeslice, the longer a thread is running
 *    the less priority it gets, however longer timeslices it gets.
 */

#define __MODULE "scheduler"
//#define __TRACE

#include <assert.h>
#include <arch/thread.h>
#include <arch/utils.h>
#include <component/domain.h>
#include <component/timer.h>
#include <debug.h>
#include <ds/list.h>
#include <ddk/io.h>
#include <heap.h>
#include <machine.h>
#include <scheduler.h>
#include <string.h>

#define EVENT_EXECUTE      0
#define EVENT_QUEUE        1
#define EVENT_QUEUE_FINISH 2
#define EVENT_BLOCK        3
#define EVENT_SCHEDULE     4

#define STATE_INVALID  0
#define STATE_INITIAL  1
#define STATE_QUEUEING 2 // Transition State
#define STATE_QUEUED   3
#define STATE_BLOCKING 4 // Transition State
#define STATE_BLOCKED  5
#define STATE_RUNNING  6

// all timing units are in nanoseconds
typedef struct SchedulerObject {
    element_t               Header;
    _Atomic(int)            State;
    unsigned int            Flags;
    UUId_t                  CoreId;
    clock_t                 TimeSlice;
    clock_t                 TimeSliceLeft;
    int                     Queue;
    struct SchedulerObject* Link;
    void*                   Object;
    
    list_t*                 WaitQueueHandle;
    clock_t                 TimeLeft;
    OsStatus_t              TimeoutReason;
    clock_t                 InterruptedAt;
} SchedulerObject_t;

static struct Transition {
    int SourceState;
    int Event;
    int TargetState;
} StateTransitions[] = {
    { STATE_INITIAL,  EVENT_QUEUE,        STATE_QUEUEING },
    { STATE_QUEUEING, EVENT_QUEUE_FINISH, STATE_QUEUED   },
    { STATE_QUEUED,   EVENT_EXECUTE,      STATE_RUNNING  },
    { STATE_RUNNING,  EVENT_SCHEDULE,     STATE_QUEUEING },
    { STATE_RUNNING,  EVENT_BLOCK,        STATE_BLOCKING },
    { STATE_BLOCKING, EVENT_QUEUE,        STATE_RUNNING  },
    { STATE_BLOCKING, EVENT_SCHEDULE,     STATE_BLOCKED  },
    { STATE_BLOCKED,  EVENT_QUEUE,        STATE_QUEUEING }
};

#ifdef __TRACE
static char* EventDescriptions[] = {
    "EVENT_EXECUTE",
    "EVENT_QUEUE",
    "EVENT_QUEUE_FINISH",
    "EVENT_BLOCK",
    "EVENT_SCHEDULE"
};

static char* StateDescriptions[] = {
    "STATE_INVALID",
    "STATE_INITIAL",
    "STATE_QUEUEING",
    "STATE_QUEUED",
    "STATE_BLOCKING",
    "STATE_BLOCKED",
    "STATE_RUNNING"
};
#endif

static int
GetAvailableTransition(
    _In_ int State,
    _In_ int Event)
{
    int i;
    for (i = 0; i < SIZEOF_ARRAY(StateTransitions); i++) {
        struct Transition* Transition = &StateTransitions[i];
        if (Transition->SourceState == State && Transition->Event == Event) {
            return Transition->TargetState;            
        }
    }
    return STATE_INVALID;
}

static int
ExecuteEvent(
    _In_ SchedulerObject_t* schedulerObject,
    _In_ int                event)
{
    int state = atomic_load(&schedulerObject->State);
    int resultState;
    int update;
    
TryAgain:
    resultState = GetAvailableTransition(state, event);
    if (resultState != STATE_INVALID) {
        update = atomic_compare_exchange_strong(&schedulerObject->State, &state, resultState);
        if (!update) {
            goto TryAgain;
        }
    }
    else {
        TRACE("[scheduler] [execute_event] unhandled %s in %s",
              EventDescriptions[event], StateDescriptions[state]);
    }

    TRACE("[scheduler] [execute_event] %s, %s => %s",
          EventDescriptions[event], StateDescriptions[state], StateDescriptions[resultState]);
    return resultState;
}

static Scheduler_t*
SchedulerGetFromCore(
    _In_ UUId_t coreId)
{
    return CpuCoreScheduler(GetProcessorCore(coreId));
}

static SchedulerObject_t*
SchedulerGetCurrentObject(
    _In_ UUId_t coreId)
{
    Thread_t* thread = CpuCoreCurrentThread(GetProcessorCore(coreId));
    if (!thread) {
        return NULL;
    }
    return ThreadSchedulerHandle(thread);
}

static const char*
GetNameOfObject(
    _In_ SchedulerObject_t* object)
{
    Thread_t* Thread = object->Object;
    return ThreadName(Thread);
}

static void
__AppendToQueue(
    _In_ SchedulerQueue_t*  queue,
    _In_ SchedulerObject_t* start,
    _In_ SchedulerObject_t* end)
{
    // Always make sure end is pointing to nothing
    end->Link = NULL;
    
    // Get the tail pointer of the queue to append
    if (queue->Head == NULL) {
        queue->Head = start;
        queue->Tail = end;
    }
    else {
        queue->Tail->Link = start;
        queue->Tail       = end;
    }
}

static OsStatus_t
__RemoveFromQueue(
    _In_ SchedulerQueue_t*  queue,
    _In_ SchedulerObject_t* object)
{
    SchedulerObject_t* current  = queue->Head;
    SchedulerObject_t* previous = NULL;

    while (current) {
        if (current == object) {
            // Two cases, previous is NULL, or not
            if (previous == NULL) queue->Head    = current->Link;
            else previous->Link = current->Link;

            // Were we also the tail pointer?
            if (queue->Tail == current) {
                if (previous == NULL) queue->Tail = current->Link;
                else queue->Tail = previous;
            }
            
            // Reset link
            object->Link = NULL;
            return OsOK;
        }
        previous = current;
        current  = current->Link;
    }
    return OsNotExists;
}

static void
__QueueForScheduler(
        _In_ Scheduler_t*       scheduler,
        _In_ SchedulerObject_t* object,
        _In_ int                outsideAdvance)
{
    int resultState;
    
    // Verify it doesn't exist in sleep queue, and check both the case where
    // it is the end of the list and if it's not
    if (object->Link != NULL ||
        scheduler->SleepQueue.Tail == object ||
        scheduler->SleepQueue.Head == object) {
        __RemoveFromQueue(&scheduler->SleepQueue, object);
    }

    resultState = ExecuteEvent(object, EVENT_QUEUE_FINISH);
    if (resultState == STATE_INVALID) {
        FATAL(FATAL_SCOPE_KERNEL, "[scheduler] [queue] object was NOT in correct state for queueing");
    }
    __AppendToQueue(&scheduler->Queues[object->Queue], object, object);
}

static void
__QueueOnCoreFunction(
    _In_ void* context)
{
    Scheduler_t*       scheduler = CpuCoreScheduler(CpuCoreCurrent());
    SchedulerObject_t* object    = (SchedulerObject_t*)context;
    __QueueForScheduler(scheduler, object, 1);

    if (ThreadIsCurrentIdle(object->CoreId)) {
        ArchThreadYield();
    }
}

static inline OsStatus_t
__QueueObjectImmediately(
    _In_ SchedulerObject_t* object)
{
    SystemCpuCore_t* core      = CpuCoreCurrent();
    Scheduler_t*     scheduler = CpuCoreScheduler(core);
    
    // If the object is running on our core, just append it
    if (CpuCoreId(core) == object->CoreId) {
        IrqSpinlockAcquire(&scheduler->SyncObject);
        __QueueForScheduler(scheduler, object, 1);
        IrqSpinlockRelease(&scheduler->SyncObject);

        // If we are running on the idle thread, we can switch immediately, unless
        if (scheduler->Enabled && ThreadIsCurrentIdle(CpuCoreId(core))) {
            ArchThreadYield();
        }
        return OsOK;
    }
    else {
        return TxuMessageSend(object->CoreId, CpuFunctionCustom, __QueueOnCoreFunction, object, 1);
    }
}

static void
__AllocateScheduler(
    _In_ SchedulerObject_t* object)
{
    SystemDomain_t*  domain    = GetCurrentDomain();
    SystemCpu_t*     coreGroup = &GetMachine()->Processor;
    SystemCpuCore_t* iter;
    Scheduler_t*     scheduler;
    UUId_t           coreId;
    
    // Select the default core range
    if (domain != NULL) {
        // Use the core range from our domain
        coreGroup = &domain->CoreGroup;
    }
    scheduler = CpuCoreScheduler(coreGroup->Cores);
    coreId    = CpuCoreId(coreGroup->Cores);
    iter      = CpuCoreNext(coreGroup->Cores);
    
    // Allocate a processor core for this object
    while (iter) {
        Scheduler_t*  iterScheduler = CpuCoreScheduler(iter);
        unsigned long bandwidth1;
        unsigned long bandwidth2;
        
        // Skip cores not booted yet, their scheduler is not initialized
        smp_rmb();
        if (!(CpuCoreState(iter) & CpuStateRunning)) {
            iter = CpuCoreNext(iter);
            continue;
        }

        bandwidth1 = atomic_load(&iterScheduler->Bandwidth);
        bandwidth2 = atomic_load(&scheduler->Bandwidth);
        if (bandwidth1 < bandwidth2) {
            scheduler = iterScheduler;
            coreId    = CpuCoreId(iter);
        }

        iter = CpuCoreNext(iter);
    }
    
    // Select whatever we end up with
    object->CoreId = coreId;
    
    // Add pressure on this scheduler
    atomic_fetch_add(&scheduler->Bandwidth, object->TimeSlice);
    atomic_fetch_add(&scheduler->ObjectCount, 1);
}

SchedulerObject_t*
SchedulerCreateObject(
    _In_ void*        payload,
    _In_ unsigned int flags)
{
    SchedulerObject_t* object = kmalloc(sizeof(SchedulerObject_t));
    if (!object) {
        return NULL;
    }
    
    memset(object, 0, sizeof(SchedulerObject_t));
    ELEMENT_INIT(&object->Header, 0, object);
    object->State  = ATOMIC_VAR_INIT(STATE_INITIAL);
    object->Object = payload;

    if (flags & THREADING_IDLE) {
        object->Queue     = SCHEDULER_LEVEL_LOW;
        object->TimeSlice = SCHEDULER_TIMESLICE_INITIAL + (SCHEDULER_LEVEL_LOW * SCHEDULER_TIMESLICE_STEP);
        object->CoreId    = ArchGetProcessorCoreId();
        WRITE_VOLATILE(object->Flags, SCHEDULER_FLAG_BOUND);
        // This only happens on the running core, no need for barriers.
    }
    else {
        object->Queue     = 0;
        object->TimeSlice = SCHEDULER_TIMESLICE_INITIAL;
        __AllocateScheduler(object);
        smp_mb();
    }

    object->TimeSliceLeft = object->TimeSlice;
    return object;
}

void
SchedulerDestroyObject(
    _In_ SchedulerObject_t* object)
{
    Scheduler_t * scheduler = SchedulerGetFromCore(object->CoreId);
    
    // Remove pressure, and explicit put a memory barrier to push the
    // memory writes to other cores that allocate objects
    atomic_fetch_sub(&scheduler->Bandwidth, object->TimeSlice);
    atomic_fetch_sub(&scheduler->ObjectCount, 1);
    
    kfree(object);
}

OsStatus_t
SchedulerSleep(
    _In_  clock_t  nanoseconds,
    _Out_ clock_t* interruptedAt)
{
    SchedulerObject_t* object;
    TRACE("SchedulerSleep %" PRIuIN, nanoseconds);

    object = SchedulerGetCurrentObject(ArchGetProcessorCoreId());
    if (!object) { // This can be called before scheduler is available
        SystemTimerStall(nanoseconds);
        return OsOK;
    }

    // Since we rely on this value not being zero in cases of timeouts
    // we would a minimum value of 1
    object->TimeLeft        = MAX(nanoseconds, 1);
    object->TimeoutReason   = OsOK;
    object->InterruptedAt   = 0;
    object->WaitQueueHandle = NULL;
    
    // We don't check return state here as we can only ever be in running
    // state at this point
    (void)ExecuteEvent(object, EVENT_BLOCK);
    
    // The moment we change this while the TimeLeft is set, the
    // sleep will automatically get started
    ArchThreadYield();
    
    smp_rmb();
    if (object->TimeoutReason != OsTimeout) {
        *interruptedAt = object->InterruptedAt;
        return OsInterrupted;
    }
    return OsOK;
}

void
SchedulerBlock(
    _In_ list_t* blockQueue,
    _In_ clock_t timeout)
{
    SchedulerObject_t* object;
    TRACE("[scheduler] [block] %" PRIuIN, timeout);

    object = SchedulerGetCurrentObject(ArchGetProcessorCoreId());
    assert(object != NULL);

    object->TimeLeft        = timeout;
    object->TimeoutReason   = OsOK;
    object->InterruptedAt   = 0;
    object->WaitQueueHandle = blockQueue;

    // We don't check return state here as we can only ever be in running
    // state at this point
    (void)ExecuteEvent(object, EVENT_BLOCK);
    
    // For now the lists include a lock, which perform memory barriers
    list_append(blockQueue, &object->Header);
}

void
SchedulerExpediteObject(
    _In_ SchedulerObject_t* object)
{
    int resultState;
    TRACE("SchedulerExpediteObject()");

    resultState = ExecuteEvent(object, EVENT_QUEUE);
    if (resultState != STATE_INVALID) {
        if (object->WaitQueueHandle != NULL) {
            (void)list_remove(object->WaitQueueHandle, &object->Header);
        }

        object->TimeoutReason = OsInterrupted;
        SystemTimerGetTimestamp(&object->InterruptedAt);
        
        // Either the resulting state is RUNNING which means we cancelled the block,
        // the rest is then up to the scheduler, or we update the state to QUEUEING,
        // which means we must initiate a queue operation.
        if (resultState == STATE_QUEUEING) {
            __QueueObjectImmediately(object);
        }
    }
    else {
        TRACE("SchedulerExpediteObject object 0x%" PRIxIN " was in invalid state", object);
    }
}

OsStatus_t
SchedulerQueueObject(
    _In_ SchedulerObject_t* object)
{
    OsStatus_t osStatus = OsOK;
    int        resultState;
    
    TRACE("SchedulerQueueObject()");
    
    assert(object != NULL);

    resultState = ExecuteEvent(object, EVENT_QUEUE);
    if (resultState == STATE_INVALID) {
        WARNING("SchedulerQueueObject object %s was in invalid state", GetNameOfObject(object));
        return OsInvalidParameters;
    }
    
    // Either the resulting state is RUNNING which means we cancelled the block,
    // the rest is then up to the scheduler, or we update the state to QUEUEING,
    // which means we must initiate a queue operation.
    if (resultState == STATE_QUEUEING) {
        osStatus = __QueueObjectImmediately(object);
    }
    return osStatus;
}

int
SchedulerObjectGetQueue(
    _In_ SchedulerObject_t* object)
{
    assert(object != NULL);
    
    smp_rmb();
    return object->Queue;
}

UUId_t
SchedulerObjectGetAffinity(
    _In_ SchedulerObject_t* object)
{
    assert(object != NULL);
    return object->CoreId;
}

OsStatus_t
SchedulerGetTimeoutReason(void)
{
    SchedulerObject_t* object = SchedulerGetCurrentObject(ArchGetProcessorCoreId());
    assert(object != NULL);
    
    return object->TimeoutReason;
}

void SchedulerDisable(void)
{
    Scheduler_t* scheduler = SchedulerGetFromCore(ArchGetProcessorCoreId());

    scheduler->Enabled = 0;
}

void SchedulerEnable(void)
{
    UUId_t       coreId    = ArchGetProcessorCoreId();
    Scheduler_t* scheduler = SchedulerGetFromCore(coreId);
    if (scheduler->Enabled) {
        return;
    }

    scheduler->Enabled = 1;
    if (ThreadIsCurrentIdle(coreId)) {
        ArchThreadYield();
    }
}

static void
__UpdatePressureForObject(
        _In_ Scheduler_t*       scheduler,
        _In_ SchedulerObject_t* object,
        _In_ int                newPressureRank)
{
    if (newPressureRank != object->Queue) {
        atomic_fetch_sub(&scheduler->Bandwidth, object->TimeSlice);

        object->Queue         = newPressureRank;
        object->TimeSlice     = (newPressureRank * SCHEDULER_TIMESLICE_STEP) + SCHEDULER_TIMESLICE_INITIAL;
        object->TimeSliceLeft = object->TimeSlice;
        atomic_fetch_add(&scheduler->Bandwidth, object->TimeSlice);
    }
}

static void
__Boost(
        _In_ Scheduler_t* scheduler)
{
    for (int i = 1; i < SCHEDULER_LEVEL_CRITICAL; i++) {
        if (scheduler->Queues[i].Head) {
            __AppendToQueue(&scheduler->Queues[0],
                            scheduler->Queues[i].Head,
                            scheduler->Queues[i].Tail);
            scheduler->Queues[i].Head = NULL;
            scheduler->Queues[i].Tail = NULL;
        }
    }
}

static void
__PerformObjectTimeout(
        _In_ Scheduler_t*       scheduler,
        _In_ SchedulerObject_t* object)
{
    int resultState;

    resultState = ExecuteEvent(object, EVENT_QUEUE);
    if (resultState != STATE_INVALID) {
        if (object->WaitQueueHandle != NULL) {
            (void)list_remove(object->WaitQueueHandle, &object->Header);
        }

        object->TimeoutReason = OsTimeout;
        SystemTimerGetTimestamp(&object->InterruptedAt);
        __QueueForScheduler(scheduler, object, 0);
    }
    else {
        WARNING("__PerformObjectTimeout object 0x%" PRIxIN " was in invalid state", object);
    }
}

// The sleep list is thread-safe due to the fact that the function that removes
// from the sleep queue is only called on this core, while the function that adds
// is also only called on this core, and the list here is only iterated on this core.
static clock_t
__UpdateSleepQueue(
        _In_ Scheduler_t*       scheduler,
        _In_ SchedulerObject_t* ignoreObject,
        _In_ clock_t            nanosecondsPassed)
{
    clock_t            nextUpdate = __MASK;
    SchedulerObject_t* i          = scheduler->SleepQueue.Head;
    
    while (i) {
        if (i != ignoreObject && i->TimeLeft) {
            i->TimeLeft -= MIN(i->TimeLeft, nanosecondsPassed);
            if (!i->TimeLeft) {
                __PerformObjectTimeout(scheduler, i);
            }
        }
        
        if (i->TimeLeft) {
            nextUpdate = MIN(i->TimeLeft, nextUpdate);
        }
        i = i->Link;
    }
    return nextUpdate;
}

static void
__HandleObjectRequeue(
        _In_ Scheduler_t*       scheduler,
        _In_ SchedulerObject_t* object,
        _In_ int                preemptive)
{
    int resultState;

    resultState = ExecuteEvent(object, EVENT_SCHEDULE);
    if (resultState == STATE_INVALID) {
        FATAL(FATAL_SCOPE_KERNEL, "__HandleObjectRequeue encounted a state that was not running/blocking");
    }
    
    // Accepted outcome states currently are QUEUEING & BLOCKED
    if (resultState == STATE_QUEUEING) {
        TRACE("__HandleObjectRequeue reschedule");
        // Did it yield itself?
        if (preemptive) {
            // Nah, we interrupted it, demote it for that unless we are at max
            // priority queue.
            if (object->Queue < SCHEDULER_LEVEL_LOW) {
                __UpdatePressureForObject(scheduler, object, object->Queue + 1);
            }
        }
        __QueueForScheduler(scheduler, object, 0);
    }
    else if (object->TimeLeft != 0) {
        TRACE("__HandleObjectRequeue sleep 0x%llx (Head 0x%llx, Tail 0x%llx)",
              object->Link, scheduler->SleepQueue.Head, scheduler->SleepQueue.Tail);
        // OK, so we are blocking this object which means we won't be
        // queuing the object up again, should we track the sleep?
        __AppendToQueue(&scheduler->SleepQueue, object, object);
    }
}

void*
SchedulerAdvance(
    _In_  SchedulerObject_t* object,
    _In_  int                preemptive,
    _In_  clock_t            nanosecondsPassed,
    _Out_ clock_t*           nextDeadlineOut)
{
    Scheduler_t*       scheduler  = CpuCoreScheduler(CpuCoreCurrent());
    SchedulerObject_t* nextObject = NULL;
    clock_t            currentClock;
    clock_t            nextDeadline;
    int                i;
    TRACE("SchedulerAdvance(current 0x%llx, forced %i, ns-passed %llu)",
          object, preemptive, nanosecondsPassed);
    
    // Allow Object to be NULL but not NextDeadlineOut
    assert(nextDeadlineOut != NULL);
    
    // In one case we can skip the whole requeue etc. etc. This happens when there
    // was a sleep event before the objects time-slice is out. Adjust and continue
    if (object != NULL && preemptive && nanosecondsPassed < object->TimeSliceLeft) {
        // Steps to take here is, adjusting the current time-slice,
        // updating the sleep queue and returning the current task again
        object->TimeSliceLeft -= nanosecondsPassed;
        nextDeadline = __UpdateSleepQueue(scheduler, NULL, nanosecondsPassed);
        *nextDeadlineOut = MIN(object->TimeSliceLeft, nextDeadline);
        TRACE("SchedulerAdvance redeploy next deadline %llu", *nextDeadlineOut);
        return object->Object;
    }

    // Handle the scheduled object first. The only times it's up to this function
    // to requeue immediately is if the thread was running. Otherwise, it's because
    // we've been interrupted or blocked.
    if (object != NULL) {
        __HandleObjectRequeue(scheduler, object, preemptive);
    }
    nextDeadline = __UpdateSleepQueue(scheduler, object, nanosecondsPassed);

    // Get next object
    for (i = 0; i < SCHEDULER_LEVEL_COUNT; i++) {
        if (scheduler->Queues[i].Head != NULL) {
            nextObject = scheduler->Queues[i].Head;
            __RemoveFromQueue(&scheduler->Queues[i], nextObject);
            __UpdatePressureForObject(scheduler, nextObject, i);
            nextDeadline = MIN(nextObject->TimeSlice, nextDeadline);
            ExecuteEvent(nextObject, EVENT_EXECUTE);
            break;
        }
    }
    
    // Handle the boost timer as long as there are active objects running
    // if we run out of objects then boosting makes no sense
    if (nextObject != NULL) {
        SystemTimerGetTimestamp(&currentClock);
        
        // Handle the boost timer
        if (scheduler->LastBoost == 0) {
            scheduler->LastBoost = currentClock;
        }
        else {
            clock_t timeDiff = currentClock - scheduler->LastBoost;
            if ((timeDiff / NSEC_PER_MSEC) >= SCHEDULER_BOOST_MS) {
                __Boost(scheduler);
                scheduler->LastBoost = currentClock;
            }
        }
        *nextDeadlineOut = nextDeadline;
        TRACE("SchedulerAdvance next 0x%llx, deadline in %llu", nextObject, nextDeadline);
    }
    else {
        // Reset boost
        scheduler->LastBoost = 0;
        *nextDeadlineOut = (nextDeadline == __MASK) ? 0 : nextDeadline;
        TRACE("SchedulerAdvance no next object, deadline in %llu", *nextDeadlineOut);
    }
    
    return (nextObject == NULL) ? NULL : nextObject->Object;
}
