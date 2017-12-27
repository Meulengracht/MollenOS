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
static Scheduler_t *Schedulers[MAX_SUPPORTED_CPUS];
static SchedulerQueue_t IoQueue = { 0 };
static int SchedulerInitialized = 0;

/* SchedulerInitialize
 * Initializes state variables and global static resources. */
void
SchedulerInitialize(void)
{
    // Initialize Globals
    memset(&Schedulers[0], 0, sizeof(Schedulers));
    IoQueue.Head = NULL;
    IoQueue.Tail = NULL;
    SchedulerInitialized = 1;
}

/* SchedulerCreate
 * Creates and initializes a scheduler for the given cpu-id. */
void
SchedulerCreate(
    _In_ UUId_t Cpu)
{
	// Variables
	Scheduler_t *Scheduler  = NULL;

	// Allocate a new instance of the scheduler
    Scheduler = (Scheduler_t*)kmalloc(sizeof(Scheduler_t));
    memset(Scheduler, 0, sizeof(Scheduler_t));
    
    // Initialize members
	CriticalSectionConstruct(&Scheduler->QueueLock, CRITICALSECTION_PLAIN);
	Schedulers[Cpu] = Scheduler;
}

/* SchedulerQueueAppend 
 * Appends a single thread or a list of threads to the given queue. */
void
SchedulerQueueAppend(
    _In_ SchedulerQueue_t *Queue,
    _In_ MCoreThread_t *ThreadStart,
    _In_ MCoreThread_t *ThreadEnd)
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

/* SchedulerQueueRemove
 * Removes a single thread from the given queue. */
void
SchedulerQueueRemove(
    _In_ SchedulerQueue_t *Queue,
    _In_ MCoreThread_t *Thread)
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

/* SchedulerBoostThreads
 * Boosts all threads in the given scheduler to queue 0.
 * This is a method of avoiding intentional starvation by malicous
 * programs. */
void
SchedulerBoostThreads(
    _In_ Scheduler_t *Scheduler)
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
    _In_ MCoreThread_t *Thread,
    _In_ Flags_t Flags)
{
    // Initialize members
    Thread->Link = NULL;

    // Flag-Special-CasE:
    // System thread?
    if (Flags & THREADING_SYSTEMTHREAD) {
        Thread->Priority = PriorityCritical;
        Thread->Queue = SCHEDULER_LEVEL_CRITICAL;
        Thread->TimeSlice = SCHEDULER_TIMESLICE_INITIAL;
    }
    else if (Flags & THREADING_IDLE) {
        Thread->Priority = PriorityLow;
        Thread->Queue = SCHEDULER_LEVEL_LOW;
        Thread->TimeSlice = SCHEDULER_TIMESLICE_INITIAL
            + (SCHEDULER_LEVEL_LOW * 2);
    }
    else {
        Thread->Priority = PriorityNormal;
        Thread->Queue = 0;
        Thread->TimeSlice = SCHEDULER_TIMESLICE_INITIAL;
    }

    // Flag-Special-Case:
	// If we are CPU bound
	if (Flags & THREADING_CPUBOUND) {
		Thread->CpuId = CpuGetCurrentId();
		Thread->Flags |= THREADING_CPUBOUND;
	}
	else {
		Thread->CpuId = SCHEDULER_CPU_SELECT;
	}
}

/* SchedulerThreadQueue
 * Queues up a thread for execution. */
OsStatus_t
SchedulerThreadQueue(
    _In_ MCoreThread_t *Thread)
{
    // Variables
    Scheduler_t *Scheduler  = NULL;
	UUId_t CpuIndex         = 0;
    int i                   = 0;
    
	// Sanitize the cpu that thread needs to be bound to
	if (Thread->CpuId == SCHEDULER_CPU_SELECT) {
		while (Schedulers[i] != NULL) {
			if (Schedulers[i]->ThreadCount < Schedulers[CpuIndex]->ThreadCount) {
				CpuIndex = i;
			}
			i++;
		}
		Thread->CpuId = CpuIndex;
	}
	else {
		CpuIndex = Thread->CpuId;
    }

    // Shorthand access
    Scheduler = Schedulers[CpuIndex];

    // Debug
    TRACE("Appending thread %u to queue %i", Thread->Id, Thread->Queue);

	// The modification of a queue is a locked operation
    CriticalSectionEnter(&Scheduler->QueueLock);
    SchedulerQueueAppend(&Scheduler->Queues[Thread->Queue],
        Thread, Thread);
        Scheduler->ThreadCount++;
    CriticalSectionLeave(&Scheduler->QueueLock);
    
    // Set thread active
    THREADING_SETSTATE(Thread->Flags, THREADING_ACTIVE);

	// If the current cpu is idling, wake us up
	if (ThreadingIsCurrentTaskIdle(Thread->CpuId) != 0) {
        ThreadingWakeCpu(Thread->CpuId);
    }
    return OsSuccess;
}

/* SchedulerThreadDequeue
 * Disarms a thread from all queues and mark the thread inactive. */
OsStatus_t
SchedulerThreadDequeue(
    _In_ MCoreThread_t *Thread)
{
    // Variables
    Scheduler_t *Scheduler = NULL;

	// Sanitize queue
	if (Thread->Queue < 0) {
        return OsError;
    }

    // Debug
    TRACE("SchedulerThreadDequeue(Cpu %u, Thread %u, Queue %u)",
        Thread->CpuId, Thread->Id, Thread->Queue);

    // Instantiate variables
    Scheduler = Schedulers[Thread->CpuId];

	// Locked operation
    CriticalSectionEnter(&Scheduler->QueueLock);
    SchedulerQueueRemove(&Scheduler->Queues[Thread->Queue], Thread);
    Scheduler->ThreadCount--;
    CriticalSectionLeave(&Scheduler->QueueLock);
    
    // Set inactive
    THREADING_CLEARSTATE(Thread->Flags);
    return OsSuccess;
}

/* SchedulerThreadSleep
 * Enters the current thread into sleep-queue. Can return different
 * sleep-state results. SCHEDULER_SLEEP_OK or SCHEDULER_SLEEP_TIMEOUT. */
int
SchedulerThreadSleep(
    _In_ uintptr_t *Handle,
    _In_ size_t Timeout)
{
    // Variables
	MCoreThread_t *CurrentThread    = NULL;
	IntStatus_t InterruptStatus     = 0;
    UUId_t CurrentCpu               = 0;
    
    // Instantiate values
    CurrentCpu = CpuGetCurrentId();
    CurrentThread = ThreadingGetCurrentThread(CurrentCpu);
    assert(CurrentThread != NULL);
    
    // Debug
    TRACE("Adding thread %u to sleep queue", CurrentThread->Id);
    
    // Disable interrupts while doing this
    InterruptStatus = InterruptDisable();
    SchedulerThreadDequeue(CurrentThread);
    CurrentThread->Flags |= THREADING_TRANSITION_SLEEP;

    // Update sleep-information
    CurrentThread->Sleep.TimeLeft = Timeout;
    CurrentThread->Sleep.Timeout = 0;
    CurrentThread->Sleep.Handle = Handle;

    // Add to io-queue
    SchedulerQueueAppend(&IoQueue, CurrentThread, CurrentThread);
    InterruptRestoreState(InterruptStatus);
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
    _In_ MCoreThread_t *Thread)
{
	// Variables
	MCoreThread_t *Current = NULL;

	// Sanitize the io-queue
	if (IoQueue.Head == NULL || Thread == NULL) {
        return OsError;
    }

    // Iterate the queue
    Current = IoQueue.Head;
    while (Current) {
        if (Current == Thread) {
            break;
        }
        else {
            Current = Current->Link;
        }
    }

	// If found, remove from queue and queue
	if (Current != NULL) {
        Current->Sleep.Timeout = 0;
        Current->Sleep.Handle = NULL;
        TimersGetSystemTick(&Current->Sleep.InterruptedAt);
        SchedulerQueueRemove(&IoQueue, Current);
		return SchedulerThreadQueue(Current);
	}
	else {
        return OsError;
    }
}

/* SchedulerHandleSignal
 * Finds a sleeping thread with the given sleep-handle and wakes it. */
OsStatus_t
SchedulerHandleSignal(
    _In_ uintptr_t *Handle)
{
	// Variables
	MCoreThread_t *Current = NULL;

	// Sanitize the io-queue
	if (IoQueue.Head == NULL || Handle == NULL) {
        return OsError;
    }

    // Iterate the queue
    Current = IoQueue.Head;
    while (Current) {
        if (Current->Sleep.Handle == Handle) {
            break;
        }
        else {
            Current = Current->Link;
        }
    }

	// If found, remove from queue and queue
	if (Current != NULL) {
        Current->Sleep.Timeout = 0;
        Current->Sleep.Handle = NULL;
        Current->Sleep.TimeLeft = 0;
        TimersGetSystemTick(&Current->Sleep.InterruptedAt);
        SchedulerQueueRemove(&IoQueue, Current);
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
    _In_ uintptr_t *Handle)
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
    _In_ size_t Milliseconds)
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
}

/* SchedulerThreadSchedule 
 * This should be called by the underlying archteicture code
 * to get the next thread that is to be run. */
MCoreThread_t*
SchedulerThreadSchedule(
    _In_ UUId_t Cpu,
    _In_ MCoreThread_t *Thread,
    _In_ int Preemptive)
{
    // Variables
    Scheduler_t *Scheduler      = NULL;
	MCoreThread_t *NextThread   = NULL;
	size_t TimeSlice            = 0;
	int i                       = 0;

	// Sanitize the scheduler status
    if (SchedulerInitialized != 1 
        || Schedulers[Cpu] == NULL) {
        return Thread;
    }

    // Debug
    TRACE("SchedulerThreadSchedule(Cpu %u, Thread 0x%x, Preemptive %i)", 
        Cpu, Thread, Preemptive);

    // Instantiate pointers
    Scheduler = Schedulers[Cpu];

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
