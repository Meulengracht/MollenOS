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

#define __MODULE "SCHE"
//#define __TRACE

#include <component/domain.h>
#include <arch/thread.h>
#include <arch/interrupts.h>
#include <arch/utils.h>
#include <scheduler.h>
#include <machine.h>
#include <assert.h>
#include <timers.h>
#include <string.h>
#include <debug.h>
#include <heap.h>

static SystemScheduler_t*
SchedulerGetFromCore(
    _In_ UUId_t CoreId)
{
    return &GetProcessorCore(CoreId)->Scheduler;
}

static SchedulerObject_t*
SchedulerGetCurrentObject(
    _In_ UUId_t CoreId)
{
    return (GetProcessorCore(CoreId)->CurrentThread != NULL) ? 
        GetProcessorCore(CoreId)->CurrentThread->SchedulerObject : NULL;
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
    _In_ SystemScheduler_t* Scheduler,
    _In_ SchedulerObject_t* Object,
    _In_ int                OutsideAdvance)
{
    // Verify it doesn't exist in sleep queue, and check both the case where
    // it is the end of the list and if it's not
    if (Object->Link != NULL ||
        Scheduler->SleepQueue.Tail == Object ||
        Scheduler->SleepQueue.Head == Object) {
        RemoveFromQueue(&Scheduler->SleepQueue, Object);
    }
    
    // Because of possible race conditions between Block/Unblock
    // it could be possible that we reach here before we have yielded
    // the task that has been blocked/unblocked quickly, check if the
    // current object equals this object. This is only valid for non-regular
    // scheduling
    if (OutsideAdvance && 
        Object == SchedulerGetCurrentObject(ArchGetProcessorCoreId())) {
        // If this should happen, then we should try to stop the block
        // from occurring. So we switch the state to Running and ignore this
        Object->State = SchedulerObjectStateRunning;
        return;
    }
    
    Object->State = SchedulerObjectStateQueued;
    AppendToQueue(&Scheduler->Queues[Object->Queue], Object, Object);
}

static void
QueueOnCoreFunction(
    _In_ void* Context)
{
    SystemScheduler_t* Scheduler = &GetCurrentProcessorCore()->Scheduler;
    SchedulerObject_t* Object    = (SchedulerObject_t*)Context;
    QueueForScheduler(Scheduler, Object, 1);
    if (ThreadingIsCurrentTaskIdle(Object->CoreId)) {
        ThreadingYield();
    }
}

static inline void
QueueObjectImmediately(
    _In_ SchedulerObject_t* Object)
{
    SystemCpuCore_t* Core;
    
    // If the object is running on our core, just append it
    Core = GetCurrentProcessorCore();
    if (Core->Id == Object->CoreId) {
        dslock(&Core->Scheduler.SyncObject);
        QueueForScheduler(&Core->Scheduler, Object, 1);
        dsunlock(&Core->Scheduler.SyncObject);
        if (ThreadingIsCurrentTaskIdle(Core->Id)) {
            ThreadingYield();
        }
    }
    else {
        // Send a message to the correct core
        ExecuteProcessorCoreFunction(Object->CoreId, CpuFunctionCustom, 
            QueueOnCoreFunction, Object);
    }
}

static void
AllocateScheduler(
    _In_ SchedulerObject_t* Object)
{
    SystemDomain_t*    Domain    = GetCurrentDomain();
    SystemCpu_t*       CoreGroup = &GetMachine()->Processor;
    SystemScheduler_t* Scheduler;
    UUId_t             CoreId;
    int                i;
    
    // Select the default core range
    if (Domain != NULL) {
        // Use the core range from our domain
        CoreGroup = &Domain->CoreGroup;
    }
    Scheduler = &CoreGroup->PrimaryCore.Scheduler;
    CoreId    = CoreGroup->PrimaryCore.Id;
    
    // Allocate a processor core for this object
    for (i = 0; i < (CoreGroup->NumberOfCores - 1); i++) {
        // Skip cores not booted yet, their scheduler is not initialized
        if (CoreGroup->ApplicationCores[i].State != CpuStateRunning) {
            continue;
        }
        size_t Bw1 = atomic_load(&CoreGroup->ApplicationCores[i].Scheduler.Bandwidth);
        size_t Bw2 = atomic_load(&Scheduler->Bandwidth);

        if (Bw1 < Bw2) {
            Scheduler = &CoreGroup->ApplicationCores[i].Scheduler;
            CoreId    = CoreGroup->ApplicationCores[i].Id;
        }
    }
    
    // Select whatever we end up with
    Object->CoreId = CoreId;
    
    // Add pressure on this scheduler
    atomic_fetch_add(&Scheduler->Bandwidth, Object->TimeSlice);
    atomic_fetch_add(&Scheduler->ObjectCount, 1);
}

SchedulerObject_t*
SchedulerCreateObject(
    _In_ void*   Payload,
    _In_ Flags_t Flags)
{
    SchedulerObject_t* Object = kmalloc(sizeof(SchedulerObject_t));
    memset(Object, 0, sizeof(SchedulerObject_t));
    
    Object->State  = SchedulerObjectStateIdle;
    Object->Object = Payload;

    if (Flags & THREADING_IDLE) {
        Object->Queue      = SCHEDULER_LEVEL_LOW;
        Object->TimeSlice  = SCHEDULER_TIMESLICE_INITIAL + (SCHEDULER_LEVEL_LOW * 2);
        Object->Flags     |= SCHEDULER_FLAG_BOUND;
        Object->CoreId     = ArchGetProcessorCoreId();
    }
    else {
        Object->Queue     = 0;
        Object->TimeSlice = SCHEDULER_TIMESLICE_INITIAL;
        AllocateScheduler(Object);
    }
    return Object;
}

void
SchedulerDestroyObject(
    _In_ SchedulerObject_t* Object)
{
    SystemScheduler_t* Scheduler = SchedulerGetFromCore(Object->CoreId);
    
    // Remove pressure
    atomic_fetch_sub(&Scheduler->Bandwidth, Object->TimeSlice);
    atomic_fetch_sub(&Scheduler->ObjectCount, 1);
    kfree(Object);
}

void
SchedulerExpediteObject(
    _In_ SchedulerObject_t* Object)
{
    OsStatus_t Status;
    
    if (Object->State == SchedulerObjectStateBlocked) {
        // Either sleeping, which means we'll interrupt it immediately
        // or it's waiting for in a block queue
        if (Object->WaitQueueHandle != NULL) {
            Status = CollectionRemoveByNode(Object->WaitQueueHandle, &Object->Header);
            if (Status != OsSuccess) {
                // we are too late, it's going into queue anyway, back off
                return;
            }
        }
        
        // We removed it, activate its timeout
        TimersGetSystemTick(&Object->InterruptedAt);
        
        QueueObjectImmediately(Object);
    }
}

int
SchedulerSleep(
    _In_ size_t Milliseconds)
{
    SchedulerObject_t* Object;
    UUId_t             CoreId;
    
    CoreId = ArchGetProcessorCoreId();
    Object = SchedulerGetCurrentObject(CoreId);
    assert(Object != NULL);
    
    // Setup information for the current running object
    Object->TimeLeft        = Milliseconds;
    Object->Timeout         = 0;
    Object->InterruptedAt   = 0;
    Object->WaitQueueHandle = NULL;
    
    // The moment we change this while the TimeLeft is set, the
    // sleep will automatically get started
    Object->State = SchedulerObjectStateBlocked;
    ThreadingYield();
    return (Object->Timeout != 1) ? 
        SCHEDULER_SLEEP_INTERRUPTED : SCHEDULER_SLEEP_OK;
}

void
SchedulerUnblockObject(
    _In_ SchedulerObject_t* Object)
{
    OsStatus_t Status;
    
    if (Object->State == SchedulerObjectStateBlocked) {
        // Either sleeping, which means we'll interrupt it immediately
        // or it's waiting for in a block queue
        if (Object->WaitQueueHandle != NULL) {
            Status = CollectionRemoveByNode(Object->WaitQueueHandle, &Object->Header);
            if (Status != OsSuccess) {
                // we are too late, it's going into queue anyway, back off
                return;
            }
            
            // Reset state to running
            Object->State = SchedulerObjectStateRunning;
        }
        
        // If we are blocked, only with a timeout, it's a sleep, allow these
    }
}

OsStatus_t
SchedulerQueueObject(
    _In_ SchedulerObject_t* Object)
{
    // We only allow idle and blocked threads to be queued up again
    if (Object->State != SchedulerObjectStateIdle &&
        Object->State != SchedulerObjectStateBlocked) {
        return OsInvalidParameters;    
    }
    
    QueueObjectImmediately(Object);
    return OsSuccess;
}

static void
UpdatePressureForObject(
    _In_ SystemScheduler_t* Scheduler,
    _In_ SchedulerObject_t* Object,
    _In_ int                NewPressureRank)
{
    if (NewPressureRank != Object->Queue) {
        atomic_fetch_sub(&Scheduler->Bandwidth, Object->TimeSlice);
        
        Object->Queue     = NewPressureRank;
        Object->TimeSlice = (NewPressureRank * 2) + SCHEDULER_TIMESLICE_INITIAL;
        atomic_fetch_add(&Scheduler->Bandwidth, Object->TimeSlice);
    }
}

static void
SchedulerBoost(
    _In_ SystemScheduler_t* Scheduler)
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

static size_t
SchedulerUpdateSleepQueue(
    _In_ SystemScheduler_t* Scheduler,
    _In_ SchedulerObject_t* IgnoreObject,
    _In_ size_t             MillisecondsPassed)
{
    size_t             NextUpdate = __MASK;
    SchedulerObject_t* i          = Scheduler->SleepQueue.Head;
    SchedulerObject_t* t;
    OsStatus_t         Status;
    
    //if ((uintptr_t)i == 0x406) { __asm { xchg bx, bx }; }
    while (i) {
        if (i != IgnoreObject) {
            i->TimeLeft -= MIN(i->TimeLeft, MillisecondsPassed);
        }
        
        if (!i->TimeLeft) {
            t = i->Link;
            
            // Synchronize with the locked list
            if (i->WaitQueueHandle != NULL) {
                Status = CollectionRemoveByNode(i->WaitQueueHandle, &i->Header);
                
                // If it was already removed, then a we're experiencing a race condition
                // by another core trying to destroy the order. Prevent this by expecting an 
                // IPI for the requeue
                if (Status != OsSuccess) {
                    RemoveFromQueue(&Scheduler->SleepQueue, i);
                    i->State = SchedulerObjectStateZombie;
                    i = t;
                    continue;
                }
            }
            
            i->Timeout = 1;
            TimersGetSystemTick(&i->InterruptedAt);
            QueueForScheduler(Scheduler, i, 0);
            i = t;
        }
        else {
            NextUpdate = MIN(i->TimeLeft, NextUpdate);
            i          = i->Link;
        }
    }
    return NextUpdate;
}

void*
SchedulerAdvance(
    _In_  SchedulerObject_t* Object,
    _In_  int                Preemptive,
    _In_  size_t             MillisecondsPassed,
    _Out_ size_t*            NextDeadlineOut)
{
    SystemScheduler_t* Scheduler  = &GetCurrentProcessorCore()->Scheduler;
    SchedulerObject_t* NextObject = NULL;
    clock_t            CurrentClock;
    size_t             NextDeadline;
    int                i;
    TRACE("SchedulerAdvance(0x%llx, forced %i, %llu)", Object, Preemptive, MillisecondsPassed);
    
    // Allow Object to be NULL but not NextDeadlineOut
    assert(NextDeadlineOut != NULL);
    
    // In one case we can skip the whole requeue etc etc. This happens when there
    // was a sleep event before the objects time-slice is out. Adjust and continue
    if (Object != NULL && Preemptive && MillisecondsPassed < Object->TimeSlice) {
        // Steps to take here is, adjusting the current time-slice,
        // updating the sleep queue and returning the current task again
        atomic_fetch_sub(&Scheduler->Bandwidth, MillisecondsPassed);
        Object->TimeSlice -= MillisecondsPassed;
        NextDeadline       = SchedulerUpdateSleepQueue(Scheduler, NULL, MillisecondsPassed);
        *NextDeadlineOut   = MIN(Object->TimeSlice, NextDeadline);
        TRACE("...redeploy next deadline %llu", *NextDeadlineOut);
        return Object->Object;
    }

    // Handle the scheduled object first
    if (Object != NULL) {
        if (Object->State != SchedulerObjectStateBlocked) {
            TRACE("...reschedule");
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
            TRACE("...sleep 0x%llx (Head 0x%llx, Tail 0x%llx)", 
                Object->Link, Scheduler->SleepQueue.Head, Scheduler->SleepQueue.Tail);
            // OK, so the we are blocking this object which means we won't be
            // queuing the object up again, should we track the sleep?
            AppendToQueue(&Scheduler->SleepQueue, Object, Object);
        }
    }
    NextDeadline = SchedulerUpdateSleepQueue(Scheduler, Object, MillisecondsPassed);

    // Get next object
    for (i = 0; i < SCHEDULER_LEVEL_COUNT; i++) {
        if (Scheduler->Queues[i].Head != NULL) {
            NextObject = Scheduler->Queues[i].Head;
            RemoveFromQueue(&Scheduler->Queues[i], NextObject);
            UpdatePressureForObject(Scheduler, NextObject, i);
            NextObject->State = SchedulerObjectStateRunning;
            NextDeadline      = MIN(NextObject->TimeSlice, NextDeadline);
            break;
        }
    }
    
    // Handle the boost timer as long as there are active objects running
    // if we run out of objects then boosting makes no sense
    if (NextObject != NULL) {
        TimersGetSystemTick(&CurrentClock);
        
        // Handle the boost timer
        if (Scheduler->LastBoost == 0) {
            Scheduler->LastBoost = CurrentClock;
        }
        else {
            clock_t TimeDiff = CurrentClock - Scheduler->LastBoost;
            if (TimeDiff >= SCHEDULER_BOOST) {
                SchedulerBoost(Scheduler);
                Scheduler->LastBoost = CurrentClock;
            }
        }
        *NextDeadlineOut = NextDeadline;
        TRACE("...next 0x%llx, deadline in %llu", NextObject, NextDeadline);
    }
    else {
        // Reset boost
        Scheduler->LastBoost = 0;
        *NextDeadlineOut = (NextDeadline == __MASK) ? 0 : NextDeadline;
        TRACE("...no next object, deadline in %llu", *NextDeadlineOut);
    }
    
    return (NextObject == NULL) ? NULL : NextObject->Object;
}
