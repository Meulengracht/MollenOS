/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS Threading Interface
 * - Common routines that are platform independant to provide
 *   a flexible and generic threading platfrom
 */
#define __MODULE "MTIF"
//#define __TRACE

/* Includes 
 * - System */
#include <system/interrupts.h>
#include <system/thread.h>
#include <system/utils.h>
#include <arch.h>

#include <garbagecollector.h>
#include <process/phoenix.h>
#include <interrupts.h>
#include <threading.h>
#include <scheduler.h>
#include <debug.h>
#include <heap.h>

/* Includes
 * - Library */
#include <ds/collection.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

/* Prototypes
 * The function handler for cleanup */
OsStatus_t ThreadingReap(void *UserData);

/* Globals, we need a few variables to
 * keep track of running threads, idle threads
 * and a thread resources lock */
static CriticalSection_t ThreadGlobalLock;
static MCoreThread_t    *GlbCurrentThreads[MAX_SUPPORTED_CPUS];
static MCoreThread_t    *GlbIdleThreads[MAX_SUPPORTED_CPUS];

static UUId_t GlbThreadGcId         = 0;
static _Atomic(UUId_t) GlbThreadId  = ATOMIC_VAR_INIT(1);
static Collection_t *GlbThreads     = NULL;
static int GlbThreadingEnabled      = 0;

/* ThreadingInitialize
 * Initializes static data and allocates resources. */
OsStatus_t
ThreadingInitialize(void)
{
    // Variables
    int i;

    // Allocate resources
    GlbThreads = CollectionCreate(KeyInteger);
    GlbThreadGcId = GcRegister(ThreadingReap);
    CriticalSectionConstruct(&ThreadGlobalLock, CRITICALSECTION_PLAIN);

    // Zero all current threads out, together with idle
    for (i = 0; i < MAX_SUPPORTED_CPUS; i++) {
        GlbCurrentThreads[i] = NULL;
        GlbIdleThreads[i] = NULL;
    }

    atomic_store(&GlbThreadId, 1);
    GlbThreadingEnabled = 1;
    return OsSuccess;
}

/* ThreadingEntryPoint
 * Initializes and handles finish of the thread
 * all threads should use this entry point. No Return */
void
ThreadingEntryPoint(void)
{
	// Variables
	MCoreThread_t *Thread 	= NULL;
	UUId_t Cpu 				= 0;

	// Debug
	TRACE("ThreadingEntryPoint()");

	// Initiate values
	Cpu         = CpuGetCurrentId();
	Thread      = ThreadingGetCurrentThread(Cpu);
    if (THREADING_RUNMODE(Thread->Flags) == THREADING_KERNELMODE
        || (Thread->Flags & THREADING_SWITCHMODE)) {
	    Thread->Function(Thread->Arguments);
	    Thread->Flags |= THREADING_FINISHED;
    }
    else {
        Thread->Contexts[THREADING_CONTEXT_LEVEL1] = ContextCreate(Thread->Flags, 
            THREADING_CONTEXT_LEVEL1, (uintptr_t)Thread->Function);
        Thread->Contexts[THREADING_CONTEXT_LEVEL1]->Arguments[1] = (uint32_t)Thread->Arguments;
        Thread->Contexts[THREADING_CONTEXT_SIGNAL1] = ContextCreate(Thread->Flags, 
            THREADING_CONTEXT_SIGNAL1, 0);
	    Thread->Flags |= THREADING_TRANSITION_USERMODE;
    }

	// When we reach here thread is dones
	ThreadingYield();
	for (;;);
}

/* ThreadingEnable
 * Enables the threading system for the given cpu calling the function. */
OsStatus_t
ThreadingEnable(
    _In_ UUId_t Cpu)
{
	// Variables
	MCoreThread_t *Thread       = NULL;
	DataKey_t Key;

	// Allocate and initialize a new thread instance
	Thread = (MCoreThread_t*)kmalloc(sizeof(MCoreThread_t));
    memset(Thread, 0, sizeof(MCoreThread_t));
    
    // Initialize members
	Thread->Id = atomic_fetch_add(&GlbThreadId, 1);
	Thread->Name = strdup("idle");
	Thread->ParentId = UUID_INVALID;
	Thread->AshId = UUID_INVALID;
    Thread->Flags = THREADING_KERNELMODE | THREADING_IDLE | THREADING_CPUBOUND;
    SchedulerThreadInitialize(Thread, Thread->Flags);
	Thread->CpuId = Cpu;

	// Create a compipe
	Thread->Pipe = PipeCreate(PIPE_DEFAULT_SIZE, 0);
    Thread->SignalQueue = CollectionCreate(KeyInteger);

	// Initialize arch-dependant members
	Thread->AddressSpace = AddressSpaceCreate(ASPACE_TYPE_KERNEL);
	if (ThreadingRegister(Thread) != OsSuccess) {
        ERROR("Failed to register thread with system. Threading is not enabled.");
        // @todo
    }

	// Acquire lock to modify the list
	Key.Value = (int)Thread->Id;
	GlbCurrentThreads[Cpu] = Thread;
	GlbIdleThreads[Cpu] = Thread;
	return CollectionAppend(GlbThreads, CollectionCreateNode(Key, Thread));
}

/* ThreadingCreateThread
 * Create a new thread with the given name,
 * entry point, arguments and flags, if name 
 * is NULL, a generic name will be generated 
 * Thread is started as soon as possible */
UUId_t
ThreadingCreateThread(
    _In_ __CONST char   *Name,
    _In_ ThreadEntry_t   Function,
    _In_ void           *Arguments,
    _In_ Flags_t         Flags)
{
	// Variables
	MCoreThread_t *Thread   = NULL;
    MCoreThread_t *Parent   = NULL;
    UUId_t Cpu              = 0;
	char NameBuffer[16];
	DataKey_t Key;
    
    // Debug
    TRACE("ThreadingCreateThread(%s, 0x%x)", Name, Flags);

	// Initialize variables
	Key.Value   = (int)atomic_fetch_add(&GlbThreadId, 1);
	Cpu         = CpuGetCurrentId();
	Parent      = ThreadingGetCurrentThread(Cpu);

	// Allocate a new thread instance and 
	// zero out the allocated instance
	Thread      = (MCoreThread_t*)kmalloc(sizeof(MCoreThread_t));
	memset(Thread, 0, sizeof(MCoreThread_t));

	// Sanitize name, if NULL generate a new
	// thread name of format 'Thread X'
	if (Name == NULL) {
		memset(&NameBuffer[0], 0, sizeof(NameBuffer));
		sprintf(&NameBuffer[0], "Thread %i", Key.Value);
		Thread->Name = strdup(&NameBuffer[0]);
	}
	else {
		Thread->Name = strdup(Name);
	}

	// Initialize some basic thread information 
	// The only flags we want to copy for now are
	// the running-mode
	Thread->Id = (UUId_t)Key.Value;
	Thread->ParentId = Parent->Id;
	Thread->AshId = Parent->AshId;
	Thread->Function = Function;
	Thread->Arguments = Arguments;
	Thread->Flags = Flags;

    // Setup initial scheduler information
    SchedulerThreadInitialize(Thread, Flags);

	// Create communication members
	Thread->Pipe = PipeCreate(PIPE_DEFAULT_SIZE, 0);
    Thread->SignalQueue = CollectionCreate(KeyInteger);
    Thread->ActiveSignal.Signal = -1;

	// Flag-Special-Case
	// If it's NOT a kernel thread
	// we specify transition-mode
	if (THREADING_RUNMODE(Flags) != THREADING_KERNELMODE
        && !(Flags & THREADING_INHERIT)) {
		Thread->Flags |= THREADING_SWITCHMODE;
	}

	// Flag-Special-Case
	// Determine the address space we want
	// to initialize for this thread
	if (THREADING_RUNMODE(Flags) == THREADING_KERNELMODE) {
		Thread->AddressSpace = AddressSpaceCreate(ASPACE_TYPE_INHERIT);
	}
	else {
		Flags_t ASFlags = 0;

		if (THREADING_RUNMODE(Flags) == THREADING_DRIVERMODE) {
			ASFlags |= ASPACE_TYPE_DRIVER;
		}
		else {
			ASFlags |= ASPACE_TYPE_APPLICATION;
		}
		if (Flags & THREADING_INHERIT) {
			ASFlags |= ASPACE_TYPE_INHERIT;
        }
		Thread->AddressSpace = AddressSpaceCreate(ASFlags);
	}

    // Create context's neccessary
    Thread->Contexts[THREADING_CONTEXT_LEVEL0] = 
        ContextCreate(Thread->Flags, THREADING_CONTEXT_LEVEL0, (uintptr_t)&ThreadingEntryPoint);
    Thread->Contexts[THREADING_CONTEXT_SIGNAL0] = 
        ContextCreate(Thread->Flags, THREADING_CONTEXT_SIGNAL0, 0);
    if (ThreadingRegister(Thread) != OsSuccess) {
        ERROR("Failed to register a new thread with system.");
        // @todo
    }

	// Append it to list & scheduler
	Key.Value = (int)Thread->Id;
	CriticalSectionEnter(&ThreadGlobalLock);
	CollectionAppend(GlbThreads, CollectionCreateNode(Key, Thread));
	CriticalSectionLeave(&ThreadGlobalLock);

    SchedulerThreadQueue(Thread);
	return Thread->Id;
}

/* ThreadingCleanupThread
 * Cleans up a thread and all it's resources, the
 * address space is not cleaned up untill all threads
 * in the given space has been shut down. Must be
 * called from a seperate thread */
void
ThreadingCleanupThread(
    _In_ MCoreThread_t *Thread)
{
    // Variables
    CollectionItem_t *fNode = NULL;

    // Make sure we are completely removed as reference
    // from the entire system
    SchedulerThreadDequeue(Thread);
	ThreadingUnregister(Thread);

    // Cleanup signals
    _foreach(fNode, Thread->SignalQueue) {
        kfree(fNode->Data);
    }
    CollectionDestroy(Thread->SignalQueue);

	// Cleanup resources allocated by sub-systems
	AddressSpaceDestroy(Thread->AddressSpace);

	// Cleanup our allocated resources
	PipeDestroy(Thread->Pipe);
	kfree((void*)Thread->Name);
	kfree(Thread);
}

/* ThreadingExitThread
 * Exits the current thread by marking it finished
 * and yielding control to scheduler */
void
ThreadingExitThread(
    _In_ int ExitCode)
{
	// Variables
	MCoreThread_t *Thread   = NULL;
	UUId_t Cpu              = 0;

	// Instantiate some values
	Cpu     = CpuGetCurrentId();
    Thread  = ThreadingGetCurrentThread(Cpu);
    assert(Thread != NULL);

	// Update thread state
    if (Thread->Flags & THREADING_CLEANUPASH) {
        ThreadingTerminateAshThreads(Thread->AshId, 0, 0);
    }
	Thread->RetCode  = ExitCode;
    Thread->Flags   |= THREADING_FINISHED;

    // Wake people waiting for us
	SchedulerHandleSignalAll((uintptr_t*)Thread);
	ThreadingYield();
}

/* ThreadingKillThread
 * Marks the thread with the given id for finished, and it will be cleaned up
 * on next switch unless specified. The given exitcode will be stored. */
KERNELAPI
OsStatus_t
KERNELABI
ThreadingKillThread(
    _In_ UUId_t ThreadId,
    _In_ int    ExitCode,
    _In_ int    TerminateInstantly)
{
	// Variables
	MCoreThread_t *Target = ThreadingGetThread(ThreadId);

	// Security check
	if (Target == NULL || (Target->Flags & THREADING_IDLE)) {
        return OsError;
    }

    // Update thread state
	Target->Flags   |= THREADING_FINISHED;
	Target->RetCode  = ExitCode;

    // Wake-up threads waiting with ThreadJoin
	SchedulerHandleSignalAll((uintptr_t*)Target);

    // Should we instagib?
    if (TerminateInstantly) {
        if (ThreadId == ThreadingGetCurrentThreadId()) {
            ThreadingYield();
        }
        // @todo
    }
    return OsSuccess;
}

/* ThreadingJoinThread
 * Can be used to wait for a thread the return 
 * value of this function is the ret-code of the thread */
int ThreadingJoinThread(UUId_t ThreadId) {
	MCoreThread_t *Target = ThreadingGetThread(ThreadId);
	if (Target == NULL) {
		return -1;
    }
	SchedulerThreadSleep((uintptr_t*)Target, 0);
	return Target->RetCode;
}

/* ThreadingSwitchLevel
 * Initializes non-kernel mode and marks the thread
 * for transitioning, there is no return from this function */
void
ThreadingSwitchLevel(
    _In_ void *AshInfo)
{
	// Variables
	MCoreThread_t *Thread   = ThreadingGetCurrentThread(CpuGetCurrentId());
	MCoreAsh_t *Ash         = (MCoreAsh_t*)AshInfo;

	// Bind thread to process
	Thread->AshId = Ash->Id;
	Thread->Function = (ThreadEntry_t)Ash->Executable->EntryAddress;
    Thread->Arguments = NULL;
	Thread->Contexts[THREADING_CONTEXT_LEVEL1] = ContextCreate(Thread->Flags,
        THREADING_CONTEXT_LEVEL1, (uintptr_t)Thread->Function);
    Thread->Contexts[THREADING_CONTEXT_LEVEL1]->Arguments[1] = (uint32_t)Thread->Arguments;
    Thread->Contexts[THREADING_CONTEXT_SIGNAL1] = ContextCreate(Thread->Flags,
        THREADING_CONTEXT_SIGNAL1, 0);
	Thread->Flags |= THREADING_TRANSITION_USERMODE;

	// Safety-catch
	ThreadingYield();
	for (;;);
}

/* ThreadingTerminateAshThreads
 * Marks all running threads that are not detached unless specified
 * for complete and to terminate on next switch, unless specified. 
 * Returns the number of threads not killed (0 if we terminate detached). */
int
ThreadingTerminateAshThreads(
    _In_ UUId_t AshId,
    _In_ int    TerminateDetached,
    _In_ int    TerminateInstantly)
{
    int ThreadsNotKilled = 0;
	foreach(tNode, GlbThreads) {
		MCoreThread_t *Thread = (MCoreThread_t*)tNode->Data;
		if (Thread->AshId == AshId) {
            if ((Thread->Flags & THREADING_DETACHED) && !TerminateDetached) {
                // If it's a detached thread calling this method we are killing ourselves
                // and shouldn't increase
                if (Thread->Id != ThreadingGetCurrentThreadId()) {
                    Thread->Flags |= THREADING_CLEANUPASH;
                    ThreadsNotKilled++;
                }
            }
            else {
			    Thread->Flags   |= THREADING_FINISHED;
                Thread->RetCode  = -1;
                if (TerminateInstantly) {
                    // Is thread running on any cpu currently?
                }
            }
		}
	}
    return ThreadsNotKilled;
}

/* ThreadingGetCurrentThread
 * Retrieves the current thread on the given cpu
 * if there is any issues it returns NULL */
MCoreThread_t*
ThreadingGetCurrentThread(
    _In_ UUId_t Cpu)
{
	// Sanitize data first
	if (GlbThreadingEnabled != 1
		|| GlbCurrentThreads[Cpu] == NULL) {
		return NULL;
	}
	return GlbCurrentThreads[Cpu];
}

/* ThreadingGetCurrentThreadId
 * Retrives the current thread id on the current cpu
 * from the callers perspective */
UUId_t
ThreadingGetCurrentThreadId(void)
{
	UUId_t Cpu = CpuGetCurrentId();
	if (GlbCurrentThreads[Cpu] == NULL) {
		return Cpu;
	}
	if (GlbThreadingEnabled != 1) {
		return 0;
	}
	else {
		return ThreadingGetCurrentThread(Cpu)->Id;
	}
}

/* ThreadingGetThread
 * Lookup thread by the given thread-id, 
 * returns NULL if invalid */
MCoreThread_t*
ThreadingGetThread(
    _In_ UUId_t ThreadId)
{
	// Iterate thread nodes and find the correct
	foreach(tNode, GlbThreads) {
		MCoreThread_t *Thread = (MCoreThread_t*)tNode->Data;
		if (Thread->Id == ThreadId) {
			return Thread;
		}
	}
	return NULL;
}

/* ThreadingIsEnabled
 * Returns 1 if the threading system has been
 * initialized, otherwise it returns 0 */
int 
ThreadingIsEnabled(void)
{
	return GlbThreadingEnabled;
}

/* ThreadingIsCurrentTaskIdle
 * Is the given cpu running it's idle task? */
int
ThreadingIsCurrentTaskIdle(
    _In_ UUId_t Cpu)
{
	if (ThreadingIsEnabled() == 1
		&& ThreadingGetCurrentThread(Cpu)->Flags & THREADING_IDLE) {
		return 1;
	}
	else {
		return 0;
	}
}

/* ThreadingGetCurrentMode
 * Returns the current run-mode for the current
 * thread on the current cpu */
Flags_t
ThreadingGetCurrentMode(void)
{
	if (ThreadingIsEnabled() == 1) {
		return ThreadingGetCurrentThread(CpuGetCurrentId())->Flags & THREADING_MODEMASK;
	}
	else {
		return 0;
	}
}

/* ThreadingReapZombies
 * Garbage-Collector function, it reaps and
 * cleans up all threads */
OsStatus_t
ThreadingReap(
    _In_ void *UserData)
{
	// Instantiate the thread pointer
    MCoreThread_t *Thread   = (MCoreThread_t*)UserData;
    DataKey_t Key;

    // Sanity
    if (Thread == NULL) {
        return OsError;
    }
    
    // Locate and remove it from list of threads
    Key.Value = (int)Thread->Id;
    CriticalSectionEnter(&ThreadGlobalLock);
    if (CollectionRemoveByKey(GlbThreads, Key) != OsSuccess) {
        // Failed to remove the node? it didn't exist?...
    }
    CriticalSectionLeave(&ThreadGlobalLock);

	// Call the cleanup
	ThreadingCleanupThread(Thread);
	return OsSuccess;
}

/* ThreadingDebugPrint
 * Prints out debugging information about each thread
 * in the system, only active threads */
void
ThreadingDebugPrint(void)
{
	foreach(i, GlbThreads) {
        MCoreThread_t *Thread = (MCoreThread_t*)i->Data;
        if (THREADING_STATE(Thread->Flags) == THREADING_ACTIVE) {
            WRITELINE("Thread %u (%s) - Flags 0x%x, Queue %i, Timeslice %u, Cpu: %u",
                Thread->Id, Thread->Name, Thread->Flags, Thread->Queue, 
                Thread->TimeSlice, Thread->CpuId);
        }
	}
}

/* ThreadingSwitch
 * This is the thread-switch function and must be 
 * be called from the below architecture to get the
 * next thread to run */
MCoreThread_t*
ThreadingSwitch(
    _In_ UUId_t         Cpu, 
    _In_ MCoreThread_t *Current, 
    _In_ int            PreEmptive,
    _InOut_ Context_t **Context)
{
	// Variables
	MCoreThread_t *NextThread   = NULL;

    // Sanitize current thread
    assert(Current != NULL);

    // Store active context
    Current->ContextActive = *Context;
    
	// Unless this one is done..
GetNextThread:
	if ((Current->Flags & THREADING_FINISHED) || (Current->Flags & THREADING_IDLE)
		|| (Current->Flags & THREADING_TRANSITION_SLEEP))
	{
        // Handle the sleep flag
        if (Current->Flags & THREADING_TRANSITION_SLEEP) {
            Current->Flags &= ~(THREADING_TRANSITION_SLEEP);
        }

		// If the thread is finished then add it to 
		// garbagecollector
		if (Current->Flags & THREADING_FINISHED) {
			GcSignal(GlbThreadGcId, Current);
		}
        
        // Don't schedule the current
        NextThread = SchedulerThreadSchedule(Cpu, NULL, PreEmptive);
	}
	else {
        NextThread = SchedulerThreadSchedule(Cpu, Current, PreEmptive);
	}

	// Sanitize if we need to active our idle thread
	if (NextThread == NULL) {
        NextThread = GlbIdleThreads[Cpu];
    }

	// More sanity 
	// If we have caught a finished thread that
	// has been killed while scheduled, get a new
	if (NextThread->Flags & THREADING_FINISHED) {
		Current = NextThread;
		goto GetNextThread;
	}

    // Handle level switch // thread startup
    if (NextThread->Flags & THREADING_TRANSITION_USERMODE) {
		NextThread->Flags &= ~(THREADING_SWITCHMODE | THREADING_TRANSITION_USERMODE);
        NextThread->ContextActive   = NextThread->Contexts[THREADING_CONTEXT_LEVEL1];
	}
    if (NextThread->ContextActive == NULL) {
        NextThread->ContextActive   = NextThread->Contexts[THREADING_CONTEXT_LEVEL0];
    }

    // Update state of thread and cpu
    *Context                        = NextThread->ContextActive;
    GlbCurrentThreads[Cpu]          = NextThread;
	return NextThread;
}
