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

/* Includes
 * - System */
#include <component/domain.h>
#include <system/thread.h>
#include <system/interrupts.h>
#include <system/utils.h>
#include <scheduler.h>
#include <timers.h>
#include <debug.h>
#include <arch.h>
#include <heap.h>

#include <assert.h>

/* Globals
 * - State keeping variables */
static CriticalSection_t IoQueueSyncObject  = CRITICALSECTION_INITIALIZE(CRITICALSECTION_PLAIN);
static SchedulerQueue_t IoQueue             = { 0 };

/* SchedulerInitialize
 * Initializes the scheduler instance to default settings and parameters. */
void
SchedulerInitialize(void)
{
    // Zero structure and initialize members
    memset((void*)&GetCurrentProcessorCore()->Scheduler, 0, sizeof(MCoreScheduler_t));
    CriticalSectionConstruct(&GetCurrentProcessorCore()->Scheduler.QueueLock, CRITICALSECTION_PLAIN);
}

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
    
    // Get the tail pointer of the queue to append
    AppendTo = Queue->Tail;
    if (AppendTo == NULL) {
        Queue->Head = ThreadStart;
        Queue->Tail = ThreadEnd;
    }
    else {
        AppendTo->Link = ThreadStart;
        Queue->Tail = ThreadEnd;
    }

    // Null end
    ThreadEnd->Link = NULL;
}

/* SchedulerQueueFind
 * Returns OsSuccess if the function succesfully found the given thread handle. */
OsStatus_t
SchedulerQueueFind(
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
            return OsSuccess;
        }
        else {
            i = i->Link;
        }
    }
    return OsError;
}

/* SchedulerQueueFindHandle
 * Returns the thread that belongs to the given sleep handle. */
MCoreThread_t*
SchedulerQueueFindHandle(
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
        if (i->Sleep.Handle == Handle) {
            return i;
        }
        else {
            i = i->Link;
        }
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
    while (Current) {
        if (Current == Thread) {
            // Two cases, previous is NULL, or not
            if (Previous == NULL) {
                Queue->Head = Current->Link;
            }
            else {
                Previous->Link = Current->Link;
            }

            // Update tail pointer as-well
            if (Queue->Tail == Current) {
                if (Previous == NULL) {
                    Queue->Tail = Current->Link;
                }
                else {
                    Queue->Tail = Previous;
                }
            }

            // Done
            Current->Link = NULL;
            return;
        }
        else {
            Previous = Current;
            Current = Current->Link;
        }
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
    
    // Move all threads up into queue 0
    // but skip queue CRITICAL
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
    _In_ MCoreThread_t*     Thread)
{
    // Variables
    MCoreScheduler_t *Scheduler = &GetCurrentDomain()->Cpu.PrimaryCore.Scheduler;
    UUId_t CoreId               = GetCurrentDomain()->Cpu.PrimaryCore.Id;
    int i;
    
    // Sanitize the cpu that thread needs to be bound to
    if (Thread->CoreId == SCHEDULER_CPU_SELECT) {
        for (i = 0; i < (GetCurrentDomain()->Cpu.NumberOfCores - 1); i++) {
            if (GetCurrentDomain()->Cpu.ApplicationCores[i].Scheduler.ThreadCount < Scheduler->ThreadCount) {
                Scheduler   = &GetCurrentDomain()->Cpu.ApplicationCores[i].Scheduler;
                CoreId      = GetCurrentDomain()->Cpu.ApplicationCores[i].Id;
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
    CriticalSectionEnter(&Scheduler->QueueLock);
    SchedulerQueueAppend(&Scheduler->Queues[Thread->Queue], Thread, Thread);
    Scheduler->ThreadCount++;
    CriticalSectionLeave(&Scheduler->QueueLock);
    
    // Set thread active
    THREADING_SETSTATE(Thread->Flags, THREADING_ACTIVE);

    // If the current cpu is idling, wake us up
    if (ThreadingIsCurrentTaskIdle(Thread->CoreId)) {
        ThreadingWakeCpu(Thread->CoreId);
    }
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

    // Sanitize queue
    if (Thread->Queue < 0) {
        return OsError;
    }

    // Debug
    TRACE("SchedulerThreadDequeue(Cpu %u, Thread %u, Queue %u)",
        Thread->CpuId, Thread->Id, Thread->Queue);
    Scheduler = SchedulerGetFromCore(Thread->CoreId);

    // Is the thread currently in sleep?
    if ((Thread->Sleep.Handle != NULL || Thread->Sleep.TimeLeft > 0) && Thread->Sleep.Timeout != 1) {
        CriticalSectionEnter(&IoQueueSyncObject);
        SchedulerQueueRemove(&Scheduler->Queues[Thread->Queue], Thread);
        CriticalSectionLeave(&IoQueueSyncObject);
    }
    else {
        CriticalSectionEnter(&Scheduler->QueueLock);
        SchedulerQueueRemove(&Scheduler->Queues[Thread->Queue], Thread);
        CriticalSectionLeave(&Scheduler->QueueLock);
    }
    Scheduler->ThreadCount--;

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
    
    // Disable interrupts while doing this
    CriticalSectionEnter(&IoQueueSyncObject);
    SchedulerThreadDequeue(CurrentThread);
    CurrentThread->Flags |= THREADING_TRANSITION_SLEEP;

    // Update sleep-information
    CurrentThread->Sleep.TimeLeft   = Timeout;
    CurrentThread->Sleep.Timeout    = 0;
    CurrentThread->Sleep.Handle     = Handle;

    // Add to io-queue
    SchedulerQueueAppend(&IoQueue, CurrentThread, CurrentThread);
    CriticalSectionLeave(&IoQueueSyncObject);
    ThreadingYield();

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
 * queueing by utilizing the the atomic section lock. */
int
SchedulerAtomicThreadSleep(
    _In_ uintptr_t*         Handle,
    _In_ AtomicSection_t*   Section)
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
    SchedulerThreadDequeue(CurrentThread);
    CurrentThread->Flags |= THREADING_TRANSITION_SLEEP;

    // Update sleep-information
    CurrentThread->Sleep.TimeLeft   = 0;
    CurrentThread->Sleep.Timeout    = 0;
    CurrentThread->Sleep.Handle     = Handle;

    // Add to io-queue
    CriticalSectionEnter(&IoQueueSyncObject);
    SchedulerQueueAppend(&IoQueue, CurrentThread, CurrentThread);
    CriticalSectionLeave(&IoQueueSyncObject);
    AtomicSectionLeave(Section);
    ThreadingYield();

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
    if (SchedulerQueueFind(&IoQueue, Thread) == OsSuccess) {
        Thread->Sleep.Timeout = 0;
        Thread->Sleep.Handle = NULL;
        TimersGetSystemTick(&Thread->Sleep.InterruptedAt);
        CriticalSectionEnter(&IoQueueSyncObject);
        SchedulerQueueRemove(&IoQueue, Thread);
        CriticalSectionLeave(&IoQueueSyncObject);
        return SchedulerThreadQueue(Thread);
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
    MCoreThread_t *Current = SchedulerQueueFindHandle(&IoQueue, Handle);
    if (Current != NULL) {
        Current->Sleep.Timeout  = 0;
        Current->Sleep.Handle   = NULL;
        Current->Sleep.TimeLeft = 0;
        TimersGetSystemTick(&Current->Sleep.InterruptedAt);
        CriticalSectionEnter(&IoQueueSyncObject);
        SchedulerQueueRemove(&IoQueue, Current);
        CriticalSectionLeave(&IoQueueSyncObject);
        return SchedulerThreadQueue(Current);
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

    // Debug
    TRACE("SchedulerTick()");

    // Iterate the queue
    CriticalSectionEnter(&IoQueueSyncObject);
    Current = IoQueue.Head;
    while (Current) {
        if (Current->Sleep.TimeLeft != 0) {
            Current->Sleep.TimeLeft -= MIN(Milliseconds, Current->Sleep.TimeLeft);
            if (Current->Sleep.TimeLeft == 0) {
                MCoreThread_t *Next = Current->Link;
                if (Current->Sleep.Handle != NULL) {
                    Current->Sleep.Timeout = 1;
                }
                SchedulerQueueRemove(&IoQueue, Current);
                SchedulerThreadQueue(Current);
                Current = Next;
            }
            else {
                Current = Current->Link;
            }
        }
        else {
            Current = Current->Link;
        }
    }
    CriticalSectionLeave(&IoQueueSyncObject);
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

    // Sanitize the scheduler status
    if (Scheduler->ThreadCount == 0) {
        return Thread;
    }

    // Debug
    TRACE("SchedulerThreadSchedule(Cpu %u, Thread 0x%x, Preemptive %i)", 
        Cpu, Thread, Preemptive);

    // Handle the scheduled thread first
    if (Thread != NULL) {
        TimeSlice = Thread->TimeSlice;

        // Did it yield itself?
        if (Preemptive != 0) {
            if (Thread->Queue < (SCHEDULER_LEVEL_CRITICAL - 1)) {
                Thread->Queue++;
                Thread->TimeSlice = (Thread->Queue * 2) 
                    + SCHEDULER_TIMESLICE_INITIAL;
            }
        }
        SchedulerThreadQueue(Thread);
    }
    else {
        TimeSlice = SCHEDULER_TIMESLICE_INITIAL;
    }

    // This is a locked operation
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
