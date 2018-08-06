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
#define __MODULE "SCHE"
//#define __TRACE

#include <component/domain.h>
#include <system/thread.h>
#include <system/interrupts.h>
#include <system/utils.h>
#include <process/phoenix.h>
#include <scheduler.h>
#include <machine.h>
#include <assert.h>
#include <timers.h>
#include <string.h>
#include <debug.h>
#include <heap.h>

static SchedulerQueue_t IoQueue             = { 0, 0, { 0 } };

/* SchedulerQueueAppend 
 * Appends a single thread or a list of threads to the given queue. */
void
SchedulerQueueAppend(
    _In_ SchedulerQueue_t*  Queue,
    _In_ MCoreThread_t*     ThreadStart,
    _In_ MCoreThread_t*     ThreadEnd)
{
    // Variables
    MCoreThread_t *AppendTo = NULL;

    // Null end
    ThreadEnd->Link = NULL;
    
    // Get the tail pointer of the queue to append
    AppendTo = Queue->Tail;
    if (AppendTo == NULL) { Queue->Head = ThreadStart; }
    else { AppendTo->Link  = ThreadStart; }
    Queue->Tail = ThreadEnd;
}

/* FindThreadInQueue
 * Returns OsSuccess if the function succesfully found the given thread in the queue. */
OsStatus_t
FindThreadInQueue(
    _In_ SchedulerQueue_t*  Queue,
    _In_ MCoreThread_t*     Thread)
{
    MCoreThread_t *i;
    if (IoQueue.Head == NULL) {
        return OsError;
    }

    // Iterate the queue
    i = IoQueue.Head;
    while (i) {
        if (i == Thread) {
            return OsSuccess;
        }
        i = i->Link;
    }
    return OsError;
}

/* IsThreadSleeping
 * Returns OsSuccess if the function succesfully found the given thread handle. This function skips threads
 * already marked for wakeup. */
OsStatus_t
IsThreadSleeping(
    _In_ SchedulerQueue_t*  Queue,
    _In_ MCoreThread_t*     Thread)
{
    // Variables
    MCoreThread_t *i = NULL;

    // Sanitize the io-queue
    if (IoQueue.Head == NULL) {
        return OsError;
    }

    // Iterate the queue
    i = IoQueue.Head;
    while (i) {
        if (i == Thread) {
            return i->Sleep.InterruptedAt ? OsError : OsSuccess;
        }
        i = i->Link;
    }
    return OsError;
}

/* GetThreadSleepingByHandle
 * Returns the thread that belongs to the given sleep handle. This function skips threads
 * already marked for wakeup. */
MCoreThread_t*
GetThreadSleepingByHandle(
    _In_ SchedulerQueue_t*  Queue,
    _In_ uintptr_t*         Handle)
{
    // Variables
    MCoreThread_t *i = NULL;

    // Sanitize the io-queue
    if (IoQueue.Head == NULL) {
        return NULL;
    }

    // Iterate the queue
    i = IoQueue.Head;
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

/* AddToSleepQueueAndSleep 
 * Appends the given thread to the sleep queue and goes to sleep immediately. */
static OsStatus_t
AddToSleepQueueAndSleep(
    _In_ MCoreThread_t*     Thread,
    _In_ atomic_int*        Object,
    _In_ int*               ExpectedValue)
{
    MCoreThread_t *AppendTo = NULL;

    // Null end
    AtomicSectionEnter(&IoQueue.SyncObject);
    Thread->Link    = NULL;
    AppendTo        = IoQueue.Tail;

    // Append us 
    if (AppendTo == NULL) { IoQueue.Head    = Thread; }
    else {                  AppendTo->Link  = Thread; }
    IoQueue.Tail    = Thread;
    
    // Verify integrity
    if (Object != NULL && !atomic_compare_exchange_strong(Object, ExpectedValue, *ExpectedValue)) {
        // Remove us again
        if (AppendTo == NULL)   { IoQueue.Head = NULL; IoQueue.Tail = NULL; }
        else                    { IoQueue.Tail = AppendTo; AppendTo->Link = NULL; }

        AtomicSectionLeave(&IoQueue.SyncObject);
        return OsError;
    }

    // Clear thread state, set skip on requeue
    THREADING_CLEARSTATE(Thread->Flags);
    Thread->Flags |= THREADING_SKIP_REQUEUE;

    AtomicSectionLeave(&IoQueue.SyncObject);
    ThreadingYield();
    return OsSuccess;
}

/* GetThreadReadyForExecution
 * Return if found the given thread that's ready for execution. */
MCoreThread_t*
GetThreadReadyForExecution(
    _In_ UUId_t             CoreId)
{
    MCoreThread_t *i = IoQueue.Head;
    while (i) {
        if (i->CoreId == CoreId && i->Sleep.InterruptedAt) {
            return i;
        }
        i = i->Link;
    }
    return NULL;
}

/* SchedulerQueueRemove
 * Removes a single thread from the given queue. */
void
SchedulerQueueRemove(
    _In_ SchedulerQueue_t*  Queue,
    _In_ MCoreThread_t*     Thread)
{
    // Variables
    MCoreThread_t   *Current = Queue->Head,
                    *Previous = NULL;

    // Find the remove-target and unlink
    AtomicSectionEnter(&Queue->SyncObject);
    while (Current) {
        if (Current == Thread) {
            // Two cases, previous is NULL, or not
            if (Previous == NULL)   Queue->Head     = Current->Link;
            else                    Previous->Link  = Current->Link;

            // Were we also the tail pointer?
            if (Queue->Tail == Current) {
                if (Previous == NULL)   Queue->Tail = Current->Link;
                else                    Queue->Tail = Previous;
            }
            break;
        }
        Previous    = Current;
        Current     = Current->Link;
    }
    AtomicSectionLeave(&Queue->SyncObject);
}

/* SchedulerSynchronizeCore
 * Synchronizes the thread to the core by waking the target core up if neccessary. */
void
SchedulerSynchronizeCore(
    _In_ MCoreThread_t*     Thread,
    _In_ int                SuppressSynchronization)
{
    volatile SystemCpuState_t *State;
    TRACE("SchedulerSynchronizeCore(%u, %i)", Thread->CoreId, SuppressSynchronization);

    // If the current cpu is idling, wake us up
    if (Thread->CoreId != CpuGetCurrentId()) {
        State = (volatile SystemCpuState_t*)&GetProcessorCore(Thread->CoreId)->State;
        while (*State & CpuStateInterruptActive);
    }
    else if (SuppressSynchronization) {
        return;
    }

    // Perform synchronization
    if (ThreadingIsCurrentTaskIdle(Thread->CoreId)) {
        ThreadingWakeCpu(Thread->CoreId);
    }
}

/* SchedulerGetFromCore
 * Retrieves the scheduler that belongs to the given core id. */
MCoreScheduler_t*
SchedulerGetFromCore(
    _In_ UUId_t             CoreId)
{
    return &GetProcessorCore(CoreId)->Scheduler;
}

/* SchedulerBoostThreads
 * Boosts all threads in the given scheduler to queue 0.
 * This is a method of avoiding intentional starvation by malicous
 * programs. */
void
SchedulerBoostThreads(
    _In_ MCoreScheduler_t*  Scheduler)
{
    // Variables
    int i                   = 0;
    
    // Move all threads up into queue 0 but skip queue CRITICAL
    for (i = 1; i < SCHEDULER_LEVEL_CRITICAL; i++) {
        if (Scheduler->Queues[i].Head != NULL) {
            SchedulerQueueAppend(&Scheduler->Queues[0], 
                Scheduler->Queues[i].Head, Scheduler->Queues[i].Tail);
            Scheduler->Queues[i].Head = NULL;
            Scheduler->Queues[i].Tail = NULL;
        }
    }
}

/* SchedulerThreadInitialize
 * Can be called by the creation of a new thread to initalize
 * all the scheduler data for that thread. */
void
SchedulerThreadInitialize(
    _In_ MCoreThread_t*     Thread,
    _In_ Flags_t            Flags)
{
    // Initialize members
    Thread->Link = NULL;

    // Flag-Special-CasE:
    // System thread?
    if (Flags & THREADING_SYSTEMTHREAD) {
        Thread->Priority    = PriorityCritical;
        Thread->Queue       = SCHEDULER_LEVEL_CRITICAL;
        Thread->TimeSlice   = SCHEDULER_TIMESLICE_INITIAL;
    }
    else if (Flags & THREADING_IDLE) {
        Thread->Priority    = PriorityLow;
        Thread->Queue       = SCHEDULER_LEVEL_LOW;
        Thread->TimeSlice   = SCHEDULER_TIMESLICE_INITIAL + (SCHEDULER_LEVEL_LOW * 2);
    }
    else {
        Thread->Priority    = PriorityNormal;
        Thread->Queue       = 0;
        Thread->TimeSlice   = SCHEDULER_TIMESLICE_INITIAL;
    }

    // Flag-Special-Case:
    // If we are CPU bound
    if (Flags & THREADING_CPUBOUND) {
        Thread->CoreId  = CpuGetCurrentId();
        Thread->Flags  |= THREADING_CPUBOUND;
    }
    else {
        Thread->CoreId  = SCHEDULER_CPU_SELECT;
    }
}

/* SchedulerThreadQueue
 * Queues up a new thread for execution on the either least-loaded core, or the specified
 * core in the thread structure. */
OsStatus_t
SchedulerThreadQueue(
    _In_ MCoreThread_t*     Thread,
    _In_ int                SuppressSynchronization)
{
    // Variables
    SystemDomain_t *Domain      = GetCurrentDomain();
    SystemCpu_t *CoreGroup      = &GetMachine()->Processor;
    MCoreScheduler_t *Scheduler;
    UUId_t CoreId;
    int i;

    // Select the default core range
    if (Domain != NULL) {
        // Use the core range from our domain
        CoreGroup   = &Domain->CoreGroup;
    }

    // Get initial state
    Scheduler   = &CoreGroup->PrimaryCore.Scheduler;
    CoreId      = CoreGroup->PrimaryCore.Id;
    
    // Sanitize the cpu that thread needs to be bound to
    if (Thread->CoreId == SCHEDULER_CPU_SELECT) {
        for (i = 0; i < (CoreGroup->NumberOfCores - 1); i++) {
            // Skip cores not booted yet, their scheduler is not initialized
            if (CoreGroup->ApplicationCores[i].State != CpuStateRunning) {
                continue;
            }

            if (CoreGroup->ApplicationCores[i].Scheduler.ThreadCount < Scheduler->ThreadCount) {
                Scheduler   = &CoreGroup->ApplicationCores[i].Scheduler;
                CoreId      = CoreGroup->ApplicationCores[i].Id;
            }
        }
        Thread->CoreId      = CoreId;
    }
    else {
        Scheduler           = SchedulerGetFromCore(Thread->CoreId);
    }

    // Debug
    TRACE("Appending thread %u (%s) to queue %i", Thread->Id, Thread->Name, Thread->Queue);

    // The modification of a queue is a locked operation
    SchedulerQueueAppend(&Scheduler->Queues[Thread->Queue], Thread, Thread);
    Scheduler->ThreadCount++;
    
    // Set thread active
    THREADING_CLEARSTATE(Thread->Flags);
    THREADING_SETSTATE(Thread->Flags, THREADING_ACTIVE);
    SchedulerSynchronizeCore(Thread, SuppressSynchronization);
    return OsSuccess;
}

/* SchedulerThreadDequeue
 * Disarms a thread from all queues and mark the thread inactive. */
OsStatus_t
SchedulerThreadDequeue(
    _In_ MCoreThread_t*     Thread)
{
    // Variables
    MCoreScheduler_t *Scheduler = NULL;
    int Found = 0;
    assert(Thread != NULL);
    assert(Thread->Queue >= 0);
    
    // Debug
    TRACE("SchedulerThreadDequeue(Cpu %u, Thread %u, Queue %u)",
        Thread->CoreId, Thread->Id, Thread->Queue);

    Scheduler = SchedulerGetFromCore(Thread->CoreId);
    if (FindThreadInQueue(&Scheduler->Queues[Thread->Queue], Thread) == OsSuccess) {
        SchedulerQueueRemove(&Scheduler->Queues[Thread->Queue], Thread);
        Found = 1;
    }
    if (FindThreadInQueue(&IoQueue, Thread) == OsSuccess) {
        SchedulerQueueRemove(&IoQueue, Thread);
        Found = 1;
    }
    if (Found) {
        Scheduler->ThreadCount--;
    }

    // Set inactive
    THREADING_CLEARSTATE(Thread->Flags);
    return OsSuccess;
}

/* SchedulerThreadSleep
 * Enters the current thread into sleep-queue. Can return different
 * sleep-state results. SCHEDULER_SLEEP_OK or SCHEDULER_SLEEP_TIMEOUT. */
int
SchedulerThreadSleep(
    _In_ uintptr_t*         Handle,
    _In_ size_t             Timeout)
{
    // Variables
    MCoreThread_t *CurrentThread    = NULL;
    UUId_t CurrentCpu               = 0;
    
    // Instantiate values
    CurrentCpu      = CpuGetCurrentId();
    CurrentThread   = ThreadingGetCurrentThread(CurrentCpu);
    assert(CurrentThread != NULL);
    
    // Debug
    TRACE("Adding thread %u to sleep queue on 0x%x", CurrentThread->Id, Handle);
    
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

/* SchedulerAtomicThreadSleep
 * Enters the current thread into sleep-queue. This is done by using a synchronized
 * queueing by utilizing the atomic memory compares. If the value has changed before going
 * to sleep, it will return SCHEDULER_SLEEP_SYNC_FAILED. */
int
SchedulerAtomicThreadSleep(
    _In_ atomic_int*        Object,
    _In_ int*               ExpectedValue,
    _In_ size_t             Timeout)
{
    // Variables
    MCoreThread_t *CurrentThread    = NULL;
    UUId_t CurrentCpu               = 0;
    
    // Instantiate values
    CurrentCpu      = CpuGetCurrentId();
    CurrentThread   = ThreadingGetCurrentThread(CurrentCpu);
    assert(CurrentThread != NULL);
    
    // Debug
    TRACE("Adding thread %u to sleep queue on 0x%x", CurrentThread->Id, Handle);
    
    // Update sleep-information
    CurrentThread->Sleep.TimeLeft       = Timeout;
    CurrentThread->Sleep.Timeout        = 0;
    CurrentThread->Sleep.Handle         = (uintptr_t*)Object;
    CurrentThread->Sleep.InterruptedAt  = 0;
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

/* SchedulerThreadSignal
 * Finds a sleeping thread with the given thread id and wakes it. */
OsStatus_t
SchedulerThreadSignal(
    _In_ MCoreThread_t*     Thread)
{
    // Debug
    TRACE("SchedulerThreadSignal(Thread %u)", Thread->Id);
    assert(Thread != NULL);

    // If found, remove from queue and queue
    if (IsThreadSleeping(&IoQueue, Thread) == OsSuccess) {
        TimersGetSystemTick(&Thread->Sleep.InterruptedAt);
        SchedulerSynchronizeCore(Thread, 0);
        return OsSuccess;
    }
    else {
        return OsError;
    }
}

/* SchedulerHandleSignal
 * Finds a sleeping thread with the given sleep-handle and wakes it. */
OsStatus_t
SchedulerHandleSignal(
    _In_ uintptr_t*         Handle)
{
    // Variables
    MCoreThread_t *Current = GetThreadSleepingByHandle(&IoQueue, Handle);
    
    // Debug
    TRACE("SchedulerHandleSignal(Handle 0x%x)", Handle);

    if (Current != NULL) {
        TimersGetSystemTick(&Current->Sleep.InterruptedAt);
        SchedulerSynchronizeCore(Current, 0);
        return OsSuccess;
    }
    else {
        return OsError;
    }
}

/* SchedulerHandleSignalAll
 * Finds any sleeping threads on the given handle and wakes them. */
void
SchedulerHandleSignalAll(
    _In_ uintptr_t*         Handle)
{
    while (1) {
        if (SchedulerHandleSignal(Handle) == OsError) {
            break;
        }
    }
}

/* SchedulerTick
 * Iterates the io-queue and handle any threads that will timeout
 * on the tick. */
void
SchedulerTick(
    _In_ size_t             Milliseconds)
{
    // Variables
    MCoreThread_t *Current = NULL;

    // Sanitize the io-queue
    if (IoQueue.Head == NULL) {
        return;
    }

    // Iterate the queue
    Current = IoQueue.Head;
    while (Current) {
        if ((Current->Sleep.InterruptedAt == 0) && (Current->Sleep.TimeLeft != 0)) {
            Current->Sleep.TimeLeft -= MIN(Milliseconds, Current->Sleep.TimeLeft);
            if (Current->Sleep.TimeLeft == 0) {
                if (Current->Sleep.Handle != NULL) {
                    Current->Sleep.Timeout = 1;
                }
                TimersGetSystemTick(&Current->Sleep.InterruptedAt);
                SchedulerSynchronizeCore(Current, 0);
            }
        }
        Current = Current->Link;
    }
}

/* SchedulerRequeueSleepers
 * Requeues any of the sleeper threads that match the scheduler for the calling core */
void
SchedulerRequeueSleepers(
    _In_ MCoreScheduler_t*  Scheduler)
{
    // Variables
    MCoreThread_t *Thread = GetThreadReadyForExecution(CpuGetCurrentId());

    while (Thread != NULL) {
        // Remove from sleeper queue and requeue them, however never
        // requeue idle threads if they used sleep
        SchedulerQueueRemove(&IoQueue, Thread);
        if (!(Thread->Flags & THREADING_IDLE)) {
            SchedulerThreadQueue(Thread, 1);
        }
        
        // Get next
        Thread = GetThreadReadyForExecution(CpuGetCurrentId());
    }
}

/* SchedulerThreadSchedule 
 * This should be called by the underlying archteicture code
 * to get the next thread that is to be run. */
MCoreThread_t*
SchedulerThreadSchedule(
    _In_ MCoreThread_t* Thread,
    _In_ int            Preemptive)
{
    // Variables
    MCoreScheduler_t *Scheduler = &GetCurrentProcessorCore()->Scheduler;
    MCoreThread_t *NextThread   = NULL;
    size_t TimeSlice            = 0;
    int i                       = 0;

    TRACE("SchedulerThreadSchedule()");

    // Requeue threads in sleep-queue that have been waken up
    if (IoQueue.Head != NULL) {
        SchedulerRequeueSleepers(Scheduler);
    }

    // Sanitize the scheduler status
    if (Scheduler->ThreadCount == 0) {
        return Thread;
    }

    // Handle the scheduled thread first
    if (Thread != NULL) {
        if (!(Thread->Flags & THREADING_SKIP_REQUEUE)) {
            TimeSlice = Thread->TimeSlice;

            // Did it yield itself?
            if (Preemptive != 0) {
                if (Thread->Queue < (SCHEDULER_LEVEL_CRITICAL - 1)) {
                    Thread->Queue++;
                    Thread->TimeSlice = (Thread->Queue * 2) + SCHEDULER_TIMESLICE_INITIAL;
                }
            }
            SchedulerThreadQueue(Thread, 1);      
        }
        else {
            Thread->Flags &= ~(THREADING_SKIP_REQUEUE); // Clear the requeue flag
        }
    }
    else {
        TimeSlice = SCHEDULER_TIMESLICE_INITIAL;
    }

    // Handle the boost timer
    Scheduler->BoostTimer += TimeSlice;
    if (Scheduler->BoostTimer >= SCHEDULER_BOOST) {
        SchedulerBoostThreads(Scheduler);
        Scheduler->BoostTimer = 0;
    }
    
    // Get next thread
    for (i = 0; i < SCHEDULER_LEVEL_COUNT; i++) {
        if (Scheduler->Queues[i].Head != NULL) {
            NextThread = Scheduler->Queues[i].Head;
            NextThread->Queue = i;
            NextThread->TimeSlice = (i * 2) + SCHEDULER_TIMESLICE_INITIAL;
            SchedulerQueueRemove(&Scheduler->Queues[i], NextThread);
            break;
        }
    }
    return NextThread;
}
