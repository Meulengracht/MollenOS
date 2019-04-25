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

// Global io-queue that holds blocked threads
static SchedulerQueue_t IoQueue      = { NULL, NULL };
static SafeMemoryLock_t IoSyncObject = { 0 };

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

static OsStatus_t
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
            return OsSuccess;
        }
        Previous = Current;
        Current  = Current->Link;
    }
    return OsDoesNotExist;
}

static MCoreThread_t*
GetThreadSleepingByHandle(
    _In_ uintptr_t* Handle)
{
    MCoreThread_t* i = IoQueue.Head;
    while (i) {
        if (i->Sleep.Handle == Handle) {
            return i;
        }
        i = i->Link;
    }
    return NULL;
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
    dslock(&IoSyncObject);
    AppendToQueue(&IoQueue, Thread, Thread);
    if (Object != NULL && !atomic_compare_exchange_strong(Object, ExpectedValue, *ExpectedValue)) {
        (void)RemoveFromQueue(&IoQueue, Thread);
        dsunlock(&IoSyncObject);
        return OsError;
    }
    Thread->SchedulerFlags |= SCHEDULER_FLAG_BLOCK_IN_PRG;
    Thread->State           = ThreadStateBlocked;
    dsunlock(&IoSyncObject);
    ThreadingYield();
    assert(Thread->State == ThreadStateRunning);

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
        size_t Bw1 = atomic_load(&CoreGroup->ApplicationCores[i].Scheduler.Bandwidth);
        size_t Bw2 = atomic_load(&Scheduler->Bandwidth);

        if (Bw1 < Bw2) {
            Scheduler = &CoreGroup->ApplicationCores[i].Scheduler;
            CoreId    = CoreGroup->ApplicationCores[i].Id;
        }
    }
    
    // Select whatever we end up with
    Thread->CoreId = CoreId;
    
    // Add pressure on this scheduler
    atomic_fetch_add(&Scheduler->Bandwidth, Thread->TimeSlice);
    atomic_fetch_add(&Scheduler->ThreadCount, 1);
}

void
SchedulerThreadInitialize(
    _In_ MCoreThread_t* Thread,
    _In_ Flags_t        Flags)
{
    Thread->Link           = NULL;
    Thread->SchedulerFlags = 0;
    Thread->State          = ThreadStateIdle;

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
    atomic_fetch_sub(&Scheduler->Bandwidth, Thread->TimeSlice);
    atomic_fetch_sub(&Scheduler->ThreadCount, 1);
    
    // Reset some states
    Thread->Link           = NULL;
    Thread->SchedulerFlags = 0;
    Thread->State          = ThreadStateIdle;
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
    TRACE("Adding %s to sleep queue on 0x%" PRIxIN " timeout %u", 
        CurrentThread->Name, Handle, Timeout);
    
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
    TRACE("Atomically adding %s to sleep queue on 0x%" PRIxIN " timeout %u", 
        CurrentThread->Name, Object, Timeout);
    
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

static void
QueueThreadForScheduler(
    _In_ SystemScheduler_t* Scheduler,
    _In_ MCoreThread_t*     Thread)
{
    Thread->State = ThreadStateQueued;
    AppendToQueue(&Scheduler->Queues[Thread->Queue], Thread, Thread);
}

static void
QueueThreadOnCoreFunction(
    _In_ void* Context)
{
    SystemScheduler_t* Scheduler = &GetCurrentProcessorCore()->Scheduler;
    MCoreThread_t*     Thread    = (MCoreThread_t*)Context;
    TRACE("QueueThreadOnCoreFunction(%u, %s)", Thread->CoreId, Thread->Name);
    QueueThreadForScheduler(Scheduler, Thread);
    if (ThreadingIsCurrentTaskIdle(Thread->CoreId)) {
        ThreadingYield();
    }
}

static OsStatus_t
TriggerBlockedThread(
    _In_ MCoreThread_t* Thread,
    _In_ int            IsHandle)
{
    SystemCpuCore_t* Core   = GetCurrentProcessorCore();
    MCoreThread_t*   Target = Thread;
    OsStatus_t       Status;
    
    dslock(&IoSyncObject);
    if (IsHandle) {
        Target = GetThreadSleepingByHandle((uintptr_t*)Thread);
        if (Target == NULL) {
            dsunlock(&IoSyncObject);
            return OsDoesNotExist;
        }
    }
    Status = RemoveFromQueue(&IoQueue, Target);
    dsunlock(&IoSyncObject);
    
    if (Status == OsSuccess) {
        // Verify the thread has been completely removed before continuing
        while (Target->SchedulerFlags & SCHEDULER_FLAG_BLOCK_IN_PRG);
        //TRACE("..signal %s", Target->Name);

        TimersGetSystemTick(&Target->Sleep.InterruptedAt);
        if (Core->Id == Target->CoreId) {
            dslock(&Core->Scheduler.SyncObject);
            QueueThreadForScheduler(&Core->Scheduler, Target);
            dsunlock(&Core->Scheduler.SyncObject);
            ThreadingYield();
        }
        else {
            // Execute function on the target core
            ExecuteProcessorCoreFunction(Target->CoreId, CpuFunctionCustom, 
                QueueThreadOnCoreFunction, Target);
        }
    }
    return Status;
}

OsStatus_t
SchedulerThreadSignal(
    _In_ MCoreThread_t* Thread)
{
    return TriggerBlockedThread(Thread, 0);
}

OsStatus_t
SchedulerHandleSignal(
    _In_ uintptr_t* Handle)
{
    return TriggerBlockedThread((MCoreThread_t*)Handle, 1);
}

void
SchedulerHandleSignalAll(
    _In_ uintptr_t* Handle)
{
    while (1) {
        if (SchedulerHandleSignal(Handle) != OsSuccess) {
            break;
        }
    }
}

void
SchedulerTick(
    _In_ size_t Milliseconds)
{
    SystemCpuCore_t* Core   = GetCurrentProcessorCore();
    int              WakeUs = 0;
    MCoreThread_t*   i;
    MCoreThread_t*   p;
    MCoreThread_t*   t;
    
    dslock(&IoSyncObject);
    i = IoQueue.Head;
    p = NULL;
    while (i) {
        if (i->Sleep.TimeLeft != 0) {
            i->Sleep.TimeLeft -= MIN(Milliseconds, i->Sleep.TimeLeft);
            if (i->Sleep.TimeLeft == 0) {
                if (i->Sleep.Handle != NULL) {
                    i->Sleep.Timeout = 1;
                }
                t = i->Link;
                RemoveFromQueue(&IoQueue, i);

                // Is it running on this core? Lets add it directly as
                // we are already in interrupt safe context
                TimersGetSystemTick(&i->Sleep.InterruptedAt);
                if (Core->Id == i->CoreId) {
                    TRACE("..timeout %s (us %u)", i->Name, i->CoreId);
                    QueueThreadForScheduler(&Core->Scheduler, i);
                    WakeUs = 1;
                }
                else {
                    // Execute function on the target core
                    TRACE("..timeout %s (ipi %u)", i->Name, i->CoreId);
                    ExecuteProcessorCoreFunction(i->CoreId, CpuFunctionCustom, 
                        QueueThreadOnCoreFunction, i);
                }
                
                i = t;
            }
            else {
                p = i;
                i = i->Link;
            }
        }
        else {
            p = i;
            i = i->Link;
        }
    }
    dsunlock(&IoSyncObject);
    if (WakeUs) {
        ThreadingYield();
    }
}

void
SchedulerThreadQueue(
    _In_ MCoreThread_t* Thread)
{
    SystemScheduler_t* Scheduler = SchedulerGetFromCore(Thread->CoreId);
    
    TRACE("Appending (%s) to core %i", Thread->Name, Thread->CoreId);
    assert(FindThreadInQueue(&IoQueue, Thread) == OsError);
    assert(Thread->State == ThreadStateIdle);
    
    // Is the thread for this core or someone else? If the thread is for
    // the current core no need to invoke an IPI, we instead just do it in
    // a thread safe way by acquiring the scheduler lock
    if (GetCurrentProcessorCore()->Id == Thread->CoreId) {
        dslock(&Scheduler->SyncObject);
        QueueThreadForScheduler(Scheduler, Thread);
        dsunlock(&Scheduler->SyncObject);
    }
    else {
        // Execute function on the target core
        ExecuteProcessorCoreFunction(Thread->CoreId, CpuFunctionCustom, 
            QueueThreadOnCoreFunction, Thread);
    }
}

static void
UpdatePressureForThread(
    _In_ SystemScheduler_t* Scheduler,
    _In_ MCoreThread_t*     Thread,
    _In_ int                NewPressureRank)
{
    if (NewPressureRank != Thread->Queue) {
        atomic_fetch_sub(&Scheduler->Bandwidth, Thread->TimeSlice);
        
        Thread->Queue     = NewPressureRank;
        Thread->TimeSlice = (NewPressureRank * 2) + SCHEDULER_TIMESLICE_INITIAL;
        atomic_fetch_add(&Scheduler->Bandwidth, Thread->TimeSlice);
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

MCoreThread_t*
SchedulerThreadSchedule(
    _In_ MCoreThread_t* Thread,
    _In_ int            Preemptive)
{
    SystemScheduler_t* Scheduler  = &GetCurrentProcessorCore()->Scheduler;
    MCoreThread_t*     NextThread = NULL;
    clock_t            CurrentClock;
    int                i;

    // Handle the scheduled thread first
    if (Thread != NULL) {
        if (Thread->State != ThreadStateBlocked) {
            // Did it yield itself?
            if (Preemptive != 0) {
                // Nah, we interrupted it, demote it for that unless we are at max
                // priority queue.
                if (Thread->Queue < SCHEDULER_LEVEL_LOW) {
                    UpdatePressureForThread(Scheduler, Thread, Thread->Queue + 1);
                }
            }
            QueueThreadForScheduler(Scheduler, Thread);
        }
        else {
            TRACE("..skip %s", Thread->Name);
            Thread->SchedulerFlags &= ~(SCHEDULER_FLAG_BLOCK_IN_PRG);
        }
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
            RemoveFromQueue(&Scheduler->Queues[i], NextThread);
            UpdatePressureForThread(Scheduler, NextThread, i);
            NextThread->State = ThreadStateRunning;
            break;
        }
    }
    return NextThread;
}
