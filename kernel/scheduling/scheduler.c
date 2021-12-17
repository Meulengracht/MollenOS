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

#define __MODULE "scheduler"
//#define __TRACE

#include <assert.h>
#include <arch/time.h>
#include <arch/thread.h>
#include <arch/utils.h>
#include <component/domain.h>
#include <debug.h>
#include <ds/list.h>
#include <ddk/io.h>
#include <heap.h>
#include <machine.h>
#include <scheduler.h>
#include <string.h>
#include <timers.h>
#include <arch/interrupts.h>

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

typedef struct SchedulerObject {
    element_t               Header;
    _Atomic(int)            State;
    unsigned int            Flags;
    UUId_t                  CoreId;
    size_t                  TimeSlice;
    size_t                  TimeSliceLeft;
    int                     Queue;
    struct SchedulerObject* Link;
    void*                   Object;
    
    list_t*                 WaitQueueHandle;
    size_t                  TimeLeft;
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
    _In_ SchedulerObject_t* Object,
    _In_ int                Event)
{
    int State = atomic_load(&Object->State);
    int ResultState;
    int Update;
    
TryAgain:
    ResultState = GetAvailableTransition(State, Event);
    if (ResultState != STATE_INVALID) {
        Update = atomic_compare_exchange_strong(&Object->State, &State, ResultState);
        if (!Update) {
            goto TryAgain;
        }
    }
    else {
        TRACE("[scheduler] [execute_event] unhandled %s in %s", 
            EventDescriptions[Event], StateDescriptions[State]);
    }
    
    TRACE("[scheduler] [execute_event] %s, %s => %s",
        EventDescriptions[Event], StateDescriptions[State], StateDescriptions[ResultState]);
    return ResultState;
}

static Scheduler_t*
SchedulerGetFromCore(
    _In_ UUId_t CoreId)
{
    return CpuCoreScheduler(GetProcessorCore(CoreId));
}

static SchedulerObject_t*
SchedulerGetCurrentObject(
    _In_ UUId_t CoreId)
{
    Thread_t* thread = CpuCoreCurrentThread(GetProcessorCore(CoreId));
    if (!thread) {
        return NULL;
    }
    return ThreadSchedulerHandle(thread);
}

static const char*
GetNameOfObject(
    _In_ SchedulerObject_t* Object)
{
    Thread_t* Thread = Object->Object;
    return ThreadName(Thread);
}

static void
AppendToQueue(
    _In_ SchedulerQueue_t*  Queue,
    _In_ SchedulerObject_t* Start,
    _In_ SchedulerObject_t* End)
{
    // Always make sure end is pointing to nothing
    End->Link = NULL;
    
    // Get the tail pointer of the queue to append
    if (Queue->Head == NULL) {
        Queue->Head = Start;
        Queue->Tail = End;
    }
    else {
        Queue->Tail->Link = Start;
        Queue->Tail       = End;
    }
}

static OsStatus_t
RemoveFromQueue(
    _In_ SchedulerQueue_t*  Queue,
    _In_ SchedulerObject_t* Object)
{
    SchedulerObject_t* Current  = Queue->Head;
    SchedulerObject_t* Previous = NULL;

    while (Current) {
        if (Current == Object) {
            // Two cases, previous is NULL, or not
            if (Previous == NULL) Queue->Head    = Current->Link;
            else                  Previous->Link = Current->Link;

            // Were we also the tail pointer?
            if (Queue->Tail == Current) {
                if (Previous == NULL) Queue->Tail = Current->Link;
                else                  Queue->Tail = Previous;
            }
            
            // Reset link
            Object->Link = NULL;
            return OsSuccess;
        }
        Previous = Current;
        Current  = Current->Link;
    }
    return OsDoesNotExist;
}

static void
QueueForScheduler(
        _In_ Scheduler_t* Scheduler,
        _In_ SchedulerObject_t* Object,
        _In_ int                OutsideAdvance)
{
    int resultState;
    
    // Verify it doesn't exist in sleep queue, and check both the case where
    // it is the end of the list and if it's not
    if (Object->Link != NULL ||
        Scheduler->SleepQueue.Tail == Object ||
        Scheduler->SleepQueue.Head == Object) {
        RemoveFromQueue(&Scheduler->SleepQueue, Object);
    }

    resultState = ExecuteEvent(Object, EVENT_QUEUE_FINISH);
    if (resultState == STATE_INVALID) {
        FATAL(FATAL_SCOPE_KERNEL, "[scheduler] [queue] object was NOT in correct state for queueing");
    }
    AppendToQueue(&Scheduler->Queues[Object->Queue], Object, Object);
}

static void
QueueOnCoreFunction(
    _In_ void* Context)
{
    Scheduler_t*       scheduler = CpuCoreScheduler(CpuCoreCurrent());
    SchedulerObject_t* object    = (SchedulerObject_t*)Context;
    QueueForScheduler(scheduler, object, 1);

    if (ThreadIsCurrentIdle(object->CoreId)) {
        ThreadingYield();
    }
}

static inline OsStatus_t
QueueObjectImmediately(
    _In_ SchedulerObject_t* Object)
{
    SystemCpuCore_t* core      = CpuCoreCurrent();
    Scheduler_t*     scheduler = CpuCoreScheduler(core);
    
    // If the object is running on our core, just append it
    if (CpuCoreId(core) == Object->CoreId) {
        IrqSpinlockAcquire(&scheduler->SyncObject);
        QueueForScheduler(scheduler, Object, 1);
        IrqSpinlockRelease(&scheduler->SyncObject);

        // If we are running on the idle thread, we can switch immediately, unless
        if (scheduler->Enabled && ThreadIsCurrentIdle(CpuCoreId(core))) {
            ThreadingYield();
        }
        return OsSuccess;
    }
    else {
        return TxuMessageSend(Object->CoreId, CpuFunctionCustom, QueueOnCoreFunction, Object, 1);
    }
}

static void
AllocateScheduler(
    _In_ SchedulerObject_t* Object)
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
        unsigned long Bw1;
        unsigned long Bw2;
        
        // Skip cores not booted yet, their scheduler is not initialized
        smp_rmb();
        if (!(CpuCoreState(iter) & CpuStateRunning)) {
            iter = CpuCoreNext(iter);
            continue;
        }
        
        Bw1 = atomic_load(&iterScheduler->Bandwidth);
        Bw2 = atomic_load(&scheduler->Bandwidth);
        if (Bw1 < Bw2) {
            scheduler = iterScheduler;
            coreId    = CpuCoreId(iter);
        }

        iter = CpuCoreNext(iter);
    }
    
    // Select whatever we end up with
    Object->CoreId = coreId;
    
    // Add pressure on this scheduler
    atomic_fetch_add(&scheduler->Bandwidth, Object->TimeSlice);
    atomic_fetch_add(&scheduler->ObjectCount, 1);
}

SchedulerObject_t*
SchedulerCreateObject(
    _In_ void*   Payload,
    _In_ unsigned int Flags)
{
    SchedulerObject_t* Object = kmalloc(sizeof(SchedulerObject_t));
    if (!Object) {
        return NULL;
    }
    
    memset(Object, 0, sizeof(SchedulerObject_t));
    ELEMENT_INIT(&Object->Header, 0, Object);
    Object->State  = ATOMIC_VAR_INIT(STATE_INITIAL);
    Object->Object = Payload;

    if (Flags & THREADING_IDLE) {
        Object->Queue      = SCHEDULER_LEVEL_LOW;
        Object->TimeSlice  = SCHEDULER_TIMESLICE_INITIAL + (SCHEDULER_LEVEL_LOW * 2);
        Object->CoreId     = ArchGetProcessorCoreId();
        WRITE_VOLATILE(Object->Flags, SCHEDULER_FLAG_BOUND);
        // This only happens on the running core, no need for barriers.
    }
    else {
        Object->Queue     = 0;
        Object->TimeSlice = SCHEDULER_TIMESLICE_INITIAL;
        AllocateScheduler(Object);
        smp_mb();
    }
    
    Object->TimeSliceLeft = Object->TimeSlice;
    return Object;
}

void
SchedulerDestroyObject(
    _In_ SchedulerObject_t* Object)
{
    Scheduler_t * Scheduler = SchedulerGetFromCore(Object->CoreId);
    
    // Remove pressure, and explicit put a memory barrier to push these
    // memory writes to other cores that allocate objects
    atomic_fetch_sub(&Scheduler->Bandwidth, Object->TimeSlice);
    atomic_fetch_sub(&Scheduler->ObjectCount, 1);
    
    kfree(Object);
}

int
SchedulerSleep(
    _In_  size_t   milliseconds,
    _Out_ clock_t* interruptedAt)
{
    SchedulerObject_t* object;
    TRACE("[scheduler] [sleep] %" PRIuIN, milliseconds);

    object = SchedulerGetCurrentObject(ArchGetProcessorCoreId());
    if (!object) {
        // Called by the idle threads
        ArchStallProcessorCore(milliseconds);
        return SCHEDULER_SLEEP_OK;
    }

    // Since we rely on this value not being zero in cases of timeouts
    // we would a minimum value of 1
    object->TimeLeft        = MAX(milliseconds, 1);
    object->TimeoutReason   = OsSuccess;
    object->InterruptedAt   = 0;
    object->WaitQueueHandle = NULL;
    
    // We don't check return state here as we can only ever be in running
    // state at this point
    (void)ExecuteEvent(object, EVENT_BLOCK);
    
    // The moment we change this while the TimeLeft is set, the
    // sleep will automatically get started
    ThreadingYield();
    
    smp_rmb();
    if (object->TimeoutReason != OsSuccess) {
        *interruptedAt = object->InterruptedAt;
        return SCHEDULER_SLEEP_INTERRUPTED;
    }
    return SCHEDULER_SLEEP_OK;
}

void
SchedulerBlock(
    _In_ list_t* BlockQueue,
    _In_ size_t  Timeout)
{
    SchedulerObject_t* Object;
    TRACE("[scheduler] [block] %" PRIuIN, Timeout);
    
    Object = SchedulerGetCurrentObject(ArchGetProcessorCoreId());
    assert(Object != NULL);
    
    Object->TimeLeft        = Timeout;
    Object->TimeoutReason   = OsSuccess;
    Object->InterruptedAt   = 0;
    Object->WaitQueueHandle = BlockQueue;

    // We don't check return state here as we can only ever be in running
    // state at this point
    (void)ExecuteEvent(Object, EVENT_BLOCK);
    
    // For now the lists include a lock, which perform memory barriers
    list_append(BlockQueue, &Object->Header);
}

void
SchedulerExpediteObject(
    _In_ SchedulerObject_t* Object)
{
    int ResultState;
    TRACE("[scheduler] [expedite]");
    
    ResultState = ExecuteEvent(Object, EVENT_QUEUE);
    if (ResultState != STATE_INVALID) {
        if (Object->WaitQueueHandle != NULL) {
            (void)list_remove(Object->WaitQueueHandle, &Object->Header);
        }
        
        Object->TimeoutReason = OsInterrupted;
        TimersGetSystemTick(&Object->InterruptedAt);
        
        // Either the resulting state is RUNNING which means we cancelled the block,
        // the rest is then up to the scheduler, or we update the state to QUEUEING,
        // which means we must initiate a queue operation.
        if (ResultState == STATE_QUEUEING) {
            QueueObjectImmediately(Object);
        }
    }
    else {
        TRACE("[scheduler] [expedite] object 0x%" PRIxIN " was in invalid state", Object);
    }
}

OsStatus_t
SchedulerQueueObject(
    _In_ SchedulerObject_t* Object)
{
    OsStatus_t Status = OsSuccess;
    int        ResultState;
    
    TRACE("[scheduler] [queue]");
    
    assert(Object != NULL);
    
    ResultState = ExecuteEvent(Object, EVENT_QUEUE);
    if (ResultState == STATE_INVALID) {
        WARNING("[scheduler] [queue] object %s was in invalid state", GetNameOfObject(Object));
        return OsInvalidParameters;
    }
    
    // Either the resulting state is RUNNING which means we cancelled the block,
    // the rest is then up to the scheduler, or we update the state to QUEUEING,
    // which means we must initiate a queue operation.
    if (ResultState == STATE_QUEUEING) {
        Status = QueueObjectImmediately(Object);
    }
    return Status;
}

int
SchedulerObjectGetQueue(
    _In_ SchedulerObject_t* Object)
{
    assert(Object != NULL);
    
    smp_rmb();
    return Object->Queue;
}

UUId_t
SchedulerObjectGetAffinity(
    _In_ SchedulerObject_t* Object)
{
    assert(Object != NULL);
    return Object->CoreId;
}

int
SchedulerGetTimeoutReason(void)
{
    SchedulerObject_t* Object;
    
    Object = SchedulerGetCurrentObject(ArchGetProcessorCoreId());
    assert(Object != NULL);
    
    return Object->TimeoutReason;
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
        ThreadingYield();
    }
}

static void
UpdatePressureForObject(
        _In_ Scheduler_t* Scheduler,
        _In_ SchedulerObject_t* Object,
        _In_ int                NewPressureRank)
{
    if (NewPressureRank != Object->Queue) {
        atomic_fetch_sub(&Scheduler->Bandwidth, Object->TimeSlice);
        
        Object->Queue         = NewPressureRank;
        Object->TimeSlice     = (NewPressureRank * 2) + SCHEDULER_TIMESLICE_INITIAL;
        Object->TimeSliceLeft = Object->TimeSlice;
        atomic_fetch_add(&Scheduler->Bandwidth, Object->TimeSlice);
    }
}

static void
SchedulerBoost(
        _In_ Scheduler_t* Scheduler)
{
    for (int i = 1; i < SCHEDULER_LEVEL_CRITICAL; i++) {
        if (Scheduler->Queues[i].Head) {
            AppendToQueue(&Scheduler->Queues[0], 
                Scheduler->Queues[i].Head, Scheduler->Queues[i].Tail);
            Scheduler->Queues[i].Head = NULL;
            Scheduler->Queues[i].Tail = NULL;
        }
    }
}

static void
PerformObjectTimeout(
        _In_ Scheduler_t* Scheduler,
        _In_ SchedulerObject_t* Object)
{
    int ResultState;
    
    ResultState = ExecuteEvent(Object, EVENT_QUEUE);
    if (ResultState != STATE_INVALID) {
        if (Object->WaitQueueHandle != NULL) {
            (void)list_remove(Object->WaitQueueHandle, &Object->Header);
        }
        
        Object->TimeoutReason = OsTimeout;
        TimersGetSystemTick(&Object->InterruptedAt);
        QueueForScheduler(Scheduler, Object, 0);
    }
    else {
        WARNING("[scheduler] [timeout] object 0x%" PRIxIN " was in invalid state", Object);
    }
}

// The sleep list is thread-safe due to the fact that the function that removes
// from the sleep queue is only called on this core, while the function that adds
// is also only called on this core, and the list here is only iterated on this core.
static size_t
SchedulerUpdateSleepQueue(
        _In_ Scheduler_t* Scheduler,
        _In_ SchedulerObject_t* IgnoreObject,
        _In_ size_t             MillisecondsPassed)
{
    size_t             NextUpdate = __MASK;
    SchedulerObject_t* i          = Scheduler->SleepQueue.Head;
    
    while (i) {
        if (i != IgnoreObject && i->TimeLeft) {
            i->TimeLeft -= MIN(i->TimeLeft, MillisecondsPassed);
            if (!i->TimeLeft) {
                PerformObjectTimeout(Scheduler, i);
            }
        }
        
        if (i->TimeLeft) {
            NextUpdate = MIN(i->TimeLeft, NextUpdate);
        }
        i = i->Link;
    }
    return NextUpdate;
}

static void
HandleObjectRequeue(
        _In_ Scheduler_t* Scheduler,
        _In_ SchedulerObject_t* Object,
        _In_ int                Preemptive)
{
    int ResultState;
    
    ResultState = ExecuteEvent(Object, EVENT_SCHEDULE);
    if (ResultState == STATE_INVALID) {
        FATAL(FATAL_SCOPE_KERNEL, "[scheduler] [advance] encounted a state that was not running/blocking");
    }
    
    // Accepted outcome states currently are QUEUEING & BLOCKED
    if (ResultState == STATE_QUEUEING) {
        TRACE("[scheduler] [advance] reschedule");
        // Did it yield itself?
        if (Preemptive) {
            // Nah, we interrupted it, demote it for that unless we are at max
            // priority queue.
            if (Object->Queue < SCHEDULER_LEVEL_LOW) {
                UpdatePressureForObject(Scheduler, Object, Object->Queue + 1);
            }
        }
        QueueForScheduler(Scheduler, Object, 0);
    }
    else if (Object->TimeLeft != 0) {
        TRACE("[scheduler] [advance] sleep 0x%llx (Head 0x%llx, Tail 0x%llx)", 
            Object->Link, Scheduler->SleepQueue.Head, Scheduler->SleepQueue.Tail);
        // OK, so the we are blocking this object which means we won't be
        // queuing the object up again, should we track the sleep?
        AppendToQueue(&Scheduler->SleepQueue, Object, Object);
    }
}

void*
SchedulerAdvance(
    _In_  SchedulerObject_t* Object,
    _In_  int                Preemptive,
    _In_  size_t             MillisecondsPassed,
    _Out_ size_t*            NextDeadlineOut)
{
    Scheduler_t*       scheduler  = CpuCoreScheduler(CpuCoreCurrent());
    SchedulerObject_t* nextObject = NULL;
    clock_t            currentClock;
    size_t             nextDeadline;
    int                i;
    TRACE("[scheduler] [advance] current 0x%llx, forced %i, ms-passed %llu",
        Object, Preemptive, MillisecondsPassed);
    
    // Allow Object to be NULL but not NextDeadlineOut
    assert(NextDeadlineOut != NULL);
    
    // In one case we can skip the whole requeue etc etc. This happens when there
    // was a sleep event before the objects time-slice is out. Adjust and continue
    if (Object != NULL && Preemptive && MillisecondsPassed < Object->TimeSliceLeft) {
        // Steps to take here is, adjusting the current time-slice,
        // updating the sleep queue and returning the current task again
        Object->TimeSliceLeft -= MillisecondsPassed;
        nextDeadline = SchedulerUpdateSleepQueue(scheduler, NULL, MillisecondsPassed);
        *NextDeadlineOut       = MIN(Object->TimeSliceLeft, nextDeadline);
        TRACE("[scheduler] [advance] redeploy next deadline %llu", *NextDeadlineOut);
        return Object->Object;
    }

    // Handle the scheduled object first. The only times it's up to this function
    // to requeue immediately is if the thread was running. Otherwise it's because
    // we've been interrupted or blocked.
    if (Object != NULL) {
        HandleObjectRequeue(scheduler, Object, Preemptive);
    }
    nextDeadline = SchedulerUpdateSleepQueue(scheduler, Object, MillisecondsPassed);

    // Get next object
    for (i = 0; i < SCHEDULER_LEVEL_COUNT; i++) {
        if (scheduler->Queues[i].Head != NULL) {
            nextObject = scheduler->Queues[i].Head;
            RemoveFromQueue(&scheduler->Queues[i], nextObject);
            UpdatePressureForObject(scheduler, nextObject, i);
            nextDeadline = MIN(nextObject->TimeSlice, nextDeadline);
            ExecuteEvent(nextObject, EVENT_EXECUTE);
            break;
        }
    }
    
    // Handle the boost timer as long as there are active objects running
    // if we run out of objects then boosting makes no sense
    if (nextObject != NULL) {
        TimersGetSystemTick(&currentClock);
        
        // Handle the boost timer
        if (scheduler->LastBoost == 0) {
            scheduler->LastBoost = currentClock;
        }
        else {
            clock_t timeDiff = currentClock - scheduler->LastBoost;
            if (timeDiff >= SCHEDULER_BOOST) {
                SchedulerBoost(scheduler);
                scheduler->LastBoost = currentClock;
            }
        }
        *NextDeadlineOut = nextDeadline;
        TRACE("[scheduler] [advance] next 0x%llx, deadline in %llu", nextObject, nextDeadline);
    }
    else {
        // Reset boost
        scheduler->LastBoost = 0;
        *NextDeadlineOut = (nextDeadline == __MASK) ? 0 : nextDeadline;
        TRACE("[scheduler] [advance] no next object, deadline in %llu", *NextDeadlineOut);
    }
    
    return (nextObject == NULL) ? NULL : nextObject->Object;
}
