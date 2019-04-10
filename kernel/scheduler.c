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
#define __MODULE "SCHE"
//#define __TRACE
//#define DETECT_OVERRUNS

#include <component/domain.h>
#include <system/thread.h>
#include <system/interrupts.h>
#include <system/utils.h>
#include <scheduler.h>
#include <machine.h>
#include <assert.h>
#include <timers.h>
#include <string.h>
#include <debug.h>
#include <heap.h>

// Global io-queue that holds blocked threads
static SchedulerQueue_t IoQueue = { 0, 0, { 0 } };

static OsStatus_t
FindThreadInQueue(
    _In_ SchedulerQueue_t* Queue,
    _In_ MCoreThread_t*    Thread)
{
    MCoreThread_t* i = Queue->Head;
    while (i) {
        if (i == Thread) {
            return OsSuccess;
        }
        i = i->Link;
    }
    return OsError;
}

static void
AppendToQueue(
    _In_ SchedulerQueue_t* Queue,
    _In_ MCoreThread_t*    ThreadStart,
    _In_ MCoreThread_t*    ThreadEnd)
{
    // Always make sure end is pointing to nothing
    ThreadEnd->Link = NULL;
    
    // Get the tail pointer of the queue to append
    if (Queue->Head == NULL) {
        Queue->Head = ThreadStart;
        Queue->Tail = ThreadEnd;
    }
    else {
        Queue->Tail->Link = ThreadStart;
        Queue->Tail       = ThreadEnd;
    }
}

static void
RemoveFromQueue(
    _In_ SchedulerQueue_t* Queue,
    _In_ MCoreThread_t*    Thread)
{
    MCoreThread_t* Current  = Queue->Head;
    MCoreThread_t* Previous = NULL;

    while (Current) {
        if (Current == Thread) {
            // Two cases, previous is NULL, or not
            if (Previous == NULL) Queue->Head    = Current->Link;
            else                  Previous->Link = Current->Link;

            // Were we also the tail pointer?
            if (Queue->Tail == Current) {
                if (Previous == NULL) Queue->Tail = Current->Link;
                else                  Queue->Tail = Previous;
            }
            
            // Reset link
            Thread->Link = NULL;
            break;
        }
        Previous = Current;
        Current  = Current->Link;
    }
}

static OsStatus_t
IsThreadSleeping(
    _In_ MCoreThread_t* Thread)
{
    MCoreThread_t* i = IoQueue.Head;
    while (i) {
        if (i == Thread) {
            return (i->Sleep.InterruptedAt == 0) ? OsSuccess : OsError;
        }
        i = i->Link;
    }
    return OsError;
}

static MCoreThread_t*
GetThreadSleepingByHandle(
    _In_ uintptr_t* Handle)
{
    MCoreThread_t* i = IoQueue.Head;
    while (i) {
        if (i->Sleep.InterruptedAt == 0) {
            if (i->Sleep.Handle == Handle) {
                return i;
            }
        }
        i = i->Link;
    }
    return NULL;
}

static MCoreThread_t*
GetThreadReadyForExecution(
    _In_ UUId_t CoreId)
{
    MCoreThread_t* i = IoQueue.Head;
    while (i) {
        if (i->CoreId == CoreId && i->Sleep.InterruptedAt != 0) {
            return i;
        }
        i = i->Link;
    }
    return NULL;
}

static void
SchedulerSynchronizeCore(
    _In_ MCoreThread_t* Thread,
    _In_ int            SuppressSynchronization)
{
    volatile SystemCpuState_t *State;
    TRACE("SchedulerSynchronizeCore(%" PRIuIN ", %i)", 
        Thread->CoreId, SuppressSynchronization);

    // If the current cpu is idling, wake us up
    if (Thread->CoreId != ArchGetProcessorCoreId()) {
        State = (volatile SystemCpuState_t*)&GetProcessorCore(Thread->CoreId)->State;
        while (*State & CpuStateInterruptActive);
    }
    else if (SuppressSynchronization) {
        return;
    }

    // Perform synchronization
    if (ThreadingIsCurrentTaskIdle(Thread->CoreId)) {
        InterruptProcessorCore(Thread->CoreId, CpuInterruptYield);
    }
}

static SystemScheduler_t*
SchedulerGetFromCore(
    _In_ UUId_t CoreId)
{
    return &GetProcessorCore(CoreId)->Scheduler;
}

static OsStatus_t
AddToSleepQueueAndSleep(
    _In_ MCoreThread_t* Thread,
    _In_ atomic_int*    Object,
    _In_ int*           ExpectedValue)
{
    AtomicSectionEnter(&IoQueue.SyncObject);
    AppendToQueue(&IoQueue, Thread, Thread);
    if (Object != NULL && !atomic_compare_exchange_strong(Object, ExpectedValue, *ExpectedValue)) {
        RemoveFromQueue(&IoQueue, Thread);
        AtomicSectionLeave(&IoQueue.SyncObject);
        return OsError;
    }
    Thread->SchedulerFlags |= SCHEDULER_FLAG_BLOCKED | SCHEDULER_FLAG_REQUEUE;
    AtomicSectionLeave(&IoQueue.SyncObject);
    ThreadingYield();
#ifdef DETECT_OVERRUNS
    if (FindThreadInQueue(&IoQueue, Thread) != OsError) {
        ERROR("Sleep.TimeLeft %u, Sleep.Timeout %u, Sleep.Handle 0x%" PRIxIN ", Sleep.InterruptedAt %u",
            Thread->Sleep.TimeLeft, Thread->Sleep.Timeout, 
            Thread->Sleep.Handle, Thread->Sleep.InterruptedAt);
        ERROR("Thread->SchedulerFlags 0x%" PRIxIN, Thread->SchedulerFlags);
        assert(0);
    }
#endif
    return OsSuccess;
}

static void
AllocateSchedulerForThread(
    _In_ MCoreThread_t* Thread)
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
    
    // Allocate a processor core for this thread
    for (i = 0; i < (CoreGroup->NumberOfCores - 1); i++) {
        // Skip cores not booted yet, their scheduler is not initialized
        if (CoreGroup->ApplicationCores[i].State != CpuStateRunning) {
            continue;
        }

        if (CoreGroup->ApplicationCores[i].Scheduler.Bandwidth < Scheduler->Bandwidth) {
            Scheduler = &CoreGroup->ApplicationCores[i].Scheduler;
            CoreId    = CoreGroup->ApplicationCores[i].Id;
        }
    }
    
    // Select whatever we end up with
    Thread->CoreId = CoreId;
    
    // Add pressure on this scheduler
    Scheduler->Bandwidth += Thread->TimeSlice;
    Scheduler->ThreadCount++;
}

void
SchedulerThreadInitialize(
    _In_ MCoreThread_t* Thread,
    _In_ Flags_t        Flags)
{
    Thread->Link           = NULL;
    Thread->SchedulerFlags = 0;

    if (Flags & THREADING_IDLE) {
        Thread->Queue           = SCHEDULER_LEVEL_LOW;
        Thread->TimeSlice       = SCHEDULER_TIMESLICE_INITIAL + (SCHEDULER_LEVEL_LOW * 2);
        Thread->SchedulerFlags |= SCHEDULER_FLAG_BOUND;
        Thread->CoreId          = ArchGetProcessorCoreId();
    }
    else {
        Thread->Queue     = 0;
        Thread->TimeSlice = SCHEDULER_TIMESLICE_INITIAL;
        
        // Initial pressure must be set
        AllocateSchedulerForThread(Thread);
    }
}

void
SchedulerThreadFinalize(
    _In_ MCoreThread_t* Thread)
{
    SystemScheduler_t* Scheduler = SchedulerGetFromCore(Thread->CoreId);
    
    // Remove pressure
    Scheduler->Bandwidth -= Thread->TimeSlice;
    Scheduler->ThreadCount--;
}

OsStatus_t
SchedulerThreadQueue(
    _In_ MCoreThread_t* Thread,
    _In_ int            SuppressSynchronization)
{
    SystemScheduler_t* Scheduler = SchedulerGetFromCore(Thread->CoreId);
    
    assert(FindThreadInQueue(&IoQueue, Thread) == OsError);
    TRACE("Appending thread %" PRIuIN " (%s) to queue %i", Thread->Id, Thread->Name, Thread->Queue);
    
    if (SuppressSynchronization) {
        AppendToQueue(&Scheduler->Queues[Thread->Queue], Thread, Thread);   
    }
    else {
        AtomicSectionEnter(&Scheduler->Queues[Thread->Queue].SyncObject);
        AppendToQueue(&Scheduler->Queues[Thread->Queue], Thread, Thread);
        AtomicSectionLeave(&Scheduler->Queues[Thread->Queue].SyncObject);
    }
    
    Thread->SchedulerFlags &= ~(SCHEDULER_FLAG_BLOCKED);
    SchedulerSynchronizeCore(Thread, SuppressSynchronization);
    return OsSuccess;
}

int
SchedulerThreadSleep(
    _In_ uintptr_t*         Handle,
    _In_ size_t             Timeout)
{
    MCoreThread_t* CurrentThread;
    UUId_t         CoreId;
    
    CoreId        = ArchGetProcessorCoreId();
    CurrentThread = GetCurrentThreadForCore(CoreId);
    
    assert(CurrentThread != NULL);
    TRACE("Adding thread %" PRIuIN " to sleep queue on 0x%" PRIxIN "", CurrentThread->Id, Handle);
    
    // Update sleep-information
    CurrentThread->Sleep.TimeLeft       = Timeout;
    CurrentThread->Sleep.Timeout        = 0;
    CurrentThread->Sleep.Handle         = Handle;
    CurrentThread->Sleep.InterruptedAt  = 0;
    AddToSleepQueueAndSleep(CurrentThread, NULL, NULL);

    // Resolve sleep-state
    if (CurrentThread->Sleep.Timeout == 1) {
        return SCHEDULER_SLEEP_TIMEOUT;
    }
    else if (CurrentThread->Sleep.TimeLeft != 0) {
        return SCHEDULER_SLEEP_INTERRUPTED;        
    }
    else {
        return SCHEDULER_SLEEP_OK;
    }
}

int
SchedulerAtomicThreadSleep(
    _In_ atomic_int*        Object,
    _In_ int*               ExpectedValue,
    _In_ size_t             Timeout)
{
    MCoreThread_t* CurrentThread;
    UUId_t         CoreId;
    
    CoreId        = ArchGetProcessorCoreId();
    CurrentThread = GetCurrentThreadForCore(CoreId);

    assert(CurrentThread != NULL);
    TRACE("Atomically adding thread %" PRIuIN " to sleep queue on 0x%" PRIxIN "", CurrentThread->Id, Object);
    
    // Update sleep-information
    CurrentThread->Sleep.TimeLeft      = Timeout;
    CurrentThread->Sleep.Timeout       = 0;
    CurrentThread->Sleep.Handle        = (uintptr_t*)Object;
    CurrentThread->Sleep.InterruptedAt = 0;
    if (AddToSleepQueueAndSleep(CurrentThread, Object, ExpectedValue) != OsSuccess) {
        return SCHEDULER_SLEEP_SYNC_FAILED;
    }

    // Resolve sleep-state
    if (CurrentThread->Sleep.Timeout == 1) {
        return SCHEDULER_SLEEP_TIMEOUT;
    }
    else if (CurrentThread->Sleep.TimeLeft != 0) {
        return SCHEDULER_SLEEP_INTERRUPTED;        
    }
    else {
        return SCHEDULER_SLEEP_OK;
    }
}

OsStatus_t
SchedulerThreadSignal(
    _In_ MCoreThread_t* Thread)
{
    OsStatus_t Status;
    
    TRACE("SchedulerThreadSignal(Thread %" PRIuIN ")", Thread->Id);
    assert(Thread != NULL);

    AtomicSectionEnter(&IoQueue.SyncObject);
    Status = IsThreadSleeping(Thread);
    if (Status == OsSuccess) {
        TimersGetSystemTick(&Thread->Sleep.InterruptedAt);
    }
    AtomicSectionLeave(&IoQueue.SyncObject);

    if (Status == OsSuccess) {
        SchedulerSynchronizeCore(Thread, 0);
    }
    return Status;
}

OsStatus_t
SchedulerHandleSignal(
    _In_ uintptr_t* Handle)
{
    MCoreThread_t* Current;
    OsStatus_t     Status = OsError;
    
    TRACE("SchedulerHandleSignal(Handle 0x%" PRIxIN ")", Handle);

    AtomicSectionEnter(&IoQueue.SyncObject);
    Current = GetThreadSleepingByHandle(Handle);
    if (Current) {
        TimersGetSystemTick(&Current->Sleep.InterruptedAt);
        Status = OsSuccess;
    }
    AtomicSectionLeave(&IoQueue.SyncObject);
    
    if (Status == OsSuccess) {
        SchedulerSynchronizeCore(Current, 0);
    }
    return Status;
}

void
SchedulerHandleSignalAll(
    _In_ uintptr_t* Handle)
{
    while (1) {
        if (SchedulerHandleSignal(Handle) == OsError) {
            break;
        }
    }
}

void
SchedulerTick(
    _In_ size_t Milliseconds)
{
    MCoreThread_t* i = IoQueue.Head;
    while (i) {
        if ((i->Sleep.InterruptedAt == 0) && (i->Sleep.TimeLeft != 0)) {
            i->Sleep.TimeLeft -= MIN(Milliseconds, i->Sleep.TimeLeft);
            if (i->Sleep.TimeLeft == 0) {
                if (i->Sleep.Handle != NULL) {
                    i->Sleep.Timeout = 1;
                }
                TimersGetSystemTick(&i->Sleep.InterruptedAt);
                SchedulerSynchronizeCore(i, 0);
            }
        }
        i = i->Link;
    }
}

static void
UpdatePressureForThread(
    _In_ SystemScheduler_t* Scheduler,
    _In_ MCoreThread_t*     Thread,
    _In_ int                NewPressureRank)
{
    if (NewPressureRank != Thread->Queue) {
        Scheduler->Bandwidth -= Thread->TimeSlice;
        Thread->Queue         = NewPressureRank;
        Thread->TimeSlice     = (NewPressureRank * 2) + SCHEDULER_TIMESLICE_INITIAL;
        Scheduler->Bandwidth += Thread->TimeSlice;
    }
}

static void
SchedulerBoostThreads(
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

static void
SchedulerRequeueSleepers(
    _In_ SystemScheduler_t* Scheduler)
{
    MCoreThread_t* Thread;
    
    Thread = GetThreadReadyForExecution(ArchGetProcessorCoreId());
    while (Thread) {
        // Remove from sleeper queue and requeue them, however never
        // requeue idle threads if they used sleep
        RemoveFromQueue(&IoQueue, Thread);
        if (!(Thread->Flags & THREADING_IDLE)) {
            SchedulerThreadQueue(Thread, 1);
        }
        Thread = GetThreadReadyForExecution(ArchGetProcessorCoreId());
    }
}

MCoreThread_t*
SchedulerThreadSchedule(
    _In_ MCoreThread_t* Thread,
    _In_ int            Preemptive)
{
    SystemScheduler_t* Scheduler  = &GetCurrentProcessorCore()->Scheduler;
    MCoreThread_t*     NextThread = NULL;
    clock_t            CurrentClock;
    int                i;

    TRACE("SchedulerThreadSchedule()");

    // Handle the scheduled thread first
    if (Thread != NULL) {
        if (!(Thread->SchedulerFlags & SCHEDULER_FLAG_REQUEUE)) {
            // Did it yield itself?
            if (Preemptive != 0) {
                // Nah, we interrupted it, demote it for that unless we are at max
                // priority queue.
                if (Thread->Queue < SCHEDULER_LEVEL_LOW) {
                    UpdatePressureForThread(Scheduler, Thread, Thread->Queue + 1);
                }
            }
            SchedulerThreadQueue(Thread, 1);      
        }
        else {
            Thread->SchedulerFlags &= ~(SCHEDULER_FLAG_REQUEUE); // Clear the requeue flag
        }
    }

    // Requeue threads in sleep-queue that have been waken up
    if (IoQueue.Head) {
        SchedulerRequeueSleepers(Scheduler);
    }
    
    // Handle the boost timer
    TimersGetSystemTick(&CurrentClock);
    if (Scheduler->LastBoost == 0) {
        Scheduler->LastBoost = CurrentClock;
    }
    else {
        clock_t TimeDiff = CurrentClock - Scheduler->LastBoost;
        if (TimeDiff >= SCHEDULER_BOOST) {
            SchedulerBoostThreads(Scheduler);
            Scheduler->LastBoost = CurrentClock;
        }
    }
    
    // Get next thread
    for (i = 0; i < SCHEDULER_LEVEL_COUNT; i++) {
        if (Scheduler->Queues[i].Head != NULL) {
            NextThread = Scheduler->Queues[i].Head;
            UpdatePressureForThread(Scheduler, NextThread, i);
            RemoveFromQueue(&Scheduler->Queues[i], NextThread);
            break;
        }
    }
    return NextThread;
}
