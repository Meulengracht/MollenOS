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

/* Includes 
 * - System */
#include <mollenos.h>
#include <system/thread.h>
#include <system/utils.h>

#include <garbagecollector.h>
#include <process/phoenix.h>
#include <interrupts.h>
#include <threading.h>
#include <scheduler.h>
#include <ds/list.h>
#include <heap.h>
#include <log.h>

/* Includes
 * - Library */
#include <assert.h>
#include <string.h>
#include <stdio.h>

/* Prototypes
 * The function handler for cleanup */
OsStatus_t ThreadingReap(void *UserData);

/* Globals, we need a few variables to
 * keep track of running threads, idle threads
 * and a thread resources lock */
UUId_t GlbThreadGcId = 0;
UUId_t GlbThreadId = 0;
List_t *GlbThreads = NULL;
ListNode_t *GlbCurrentThreads[MAX_SUPPORTED_CPUS];
ListNode_t *GlbIdleThreads[MAX_SUPPORTED_CPUS];
Spinlock_t GlbThreadLock = SPINLOCK_INIT;
int GlbThreadingEnabled = 0;

/* ThreadingGetCurrentNode
 * Helper function, retrieves the current 
 * list-node in our list of threads */
ListNode_t *ThreadingGetCurrentNode(UUId_t Cpu) {
	return GlbCurrentThreads[Cpu];
}

/* ThreadingUpdateCurrent
 * Helper function, updates the current
 * list-node in our list of current threads */
void ThreadingUpdateCurrent(UUId_t Cpu, ListNode_t *Node) {
	GlbCurrentThreads[Cpu] = Node;
}

/* ThreadingEntryPoint
 * Initializes and handles finish of the thread
 * all threads should use this entry point. No Return */
void ThreadingEntryPoint(void)
{
	/* Variables for setup */
	MCoreThread_t *Thread = NULL;
	UUId_t Cpu = 0;

	/* Retrieve the current cpu and
	 * get the current thread */
	Cpu = CpuGetCurrentId();
	Thread = ThreadingGetCurrentThread(Cpu);

	/* We don't need futther init, run the thread */
	Thread->Function(Thread->Args);

	/* IF WE REACH THIS POINT THREAD IS DONE! */
	Thread->Flags |= THREADING_FINISHED;

	/* Yield control, with safety-catch */
	IThreadYield();
	for (;;);
}

/* ThreadingEntryPointUserMode
 * This is the userspace version of the entry point
 * and is used for initializing userspace threads 
 * Threads started like this MUST use ThreadingExitThread */
void ThreadingEntryPointUserMode(void)
{
	/* Variables for setup */
	MCoreThread_t *Thread = NULL;
	UUId_t Cpu = 0;

	/* Retrieve the current cpu and
	 * get the current thread */
	Cpu = CpuGetCurrentId();
	Thread = ThreadingGetCurrentThread(Cpu);

	/* It's important to create 
	 * and map the stack before setting up */
	AddressSpaceMap(AddressSpaceGetCurrent(), (MEMORY_SEGMENT_STACK_BASE & PAGE_MASK),
		ASH_STACK_INIT, __MASK, AS_FLAG_APPLICATION);

	/* Let the architecture know we want to enter
	 * user-mode */
	IThreadSetupUserMode(Thread, MEMORY_SEGMENT_STACK_BASE);

	/* Nothing actually happens before this flag is set */
	Thread->Flags |= THREADING_TRANSITION;

	/* Yield control, with safety-catch */
	IThreadYield();
	for (;;);
}

/* ThreadingInitialize
 * Initializes threading on the given cpu-core
 * and initializes the current 'context' as the
 * idle-thread, first time it's called it also
 * does initialization of threading system */
void ThreadingInitialize(UUId_t Cpu)
{
	/* Variables for initializing the system */
	MCoreThread_t *Init = NULL;
	ListNode_t *Node = NULL;
	DataKey_t Key;
	int Itr = 0;

	/* Acquire the lock */
	SpinlockAcquire(&GlbThreadLock);

	/* Sanitize the global, do we need to
	 * initialize the entire threading? */
	if (GlbThreadingEnabled != 1) 
	{
		/* Create threading list */
		GlbThreads = ListCreate(KeyInteger, LIST_SAFE);
		GlbThreadGcId = GcRegister(ThreadingReap);
		GlbThreadId = 0;

		/* Zero all current threads out, together with idle */
		for (Itr = 0; Itr < MAX_SUPPORTED_CPUS; Itr++) {
			GlbCurrentThreads[Itr] = NULL;
			GlbIdleThreads[Itr] = NULL;
		}

		/* Set enabled */
		GlbThreadingEnabled = 1;
	}

	/* Allocate resoures for a new thread (the idle thread for
	 * this cpu)! */
	Init = (MCoreThread_t*)kmalloc(sizeof(MCoreThread_t));
	memset(Init, 0, sizeof(MCoreThread_t));

	/* Initialize default values */
	Init->Name = strdup("idle");
	Init->Queue = MCORE_SCHEDULER_LEVELS - 1;
	Init->Flags = THREADING_IDLE | THREADING_SYSTEMTHREAD | THREADING_CPUBOUND;
	Init->TimeSlice = MCORE_IDLE_TIMESLICE;
	Init->Priority = PriorityLow;
	Init->ParentId = 0xDEADBEEF;
	Init->Id = GlbThreadId++;
	Init->AshId = PHOENIX_NO_ASH;
	Init->CpuId = Cpu;

	/* Create the pipe for communiciation */
	Init->Pipe = PipeCreate(PIPE_DEFAULT_SIZE, 0);

	/* Release the lock */
	SpinlockRelease(&GlbThreadLock);

	/* Create resource spaces for the
	 * underlying platform */
	Init->AddressSpace = AddressSpaceCreate(AS_TYPE_KERNEL);
	Init->ThreadData = IThreadCreate(Init->Flags, 0);

	/* Create a list node */
	Key.Value = (int)Init->Id;
	Node = ListCreateNode(Key, Key, Init);
	GlbCurrentThreads[Cpu] = Node;
	GlbIdleThreads[Cpu] = Node;

	/* append to thread list */
	ListAppend(GlbThreads, Node);
}

/* ThreadingCreateThread
 * Create a new thread with the given name,
 * entry point, arguments and flags, if name 
 * is NULL, a generic name will be generated 
 * Thread is started as soon as possible */
UUId_t ThreadingCreateThread(const char *Name,
	ThreadEntry_t Function, void *Arguments, Flags_t Flags)
{
	/* Variables needed for thread creation */
	MCoreThread_t *Thread = NULL, *Parent = NULL;
	char NameBuffer[16];
	DataKey_t Key;
	UUId_t Cpu = 0;

	/* Acquire the thread lock */
	SpinlockAcquire(&GlbThreadLock);

	/* Lookup current thread and cpu */
	Key.Value = (int)GlbThreadId++;
	Cpu = CpuGetCurrentId();
	Parent = ThreadingGetCurrentThread(Cpu);

	/* Release the lock, we don't need it for
	 * anything else than id */
	SpinlockRelease(&GlbThreadLock);

	/* Allocate a new thread instance and 
	 * zero out the allocated instance */
	Thread = (MCoreThread_t*)kmalloc(sizeof(MCoreThread_t));
	memset(Thread, 0, sizeof(MCoreThread_t));

	/* Sanitize name, if NULL generate a new
	 * thread name of format 'Thread X' */
	if (Name == NULL) {
		memset(&NameBuffer[0], 0, sizeof(NameBuffer));
		sprintf(&NameBuffer[0], "Thread %u", GlbThreadId);
		Thread->Name = strdup(&NameBuffer[0]);
	}
	else {
		Thread->Name = strdup(Name);
	}

	/* Initialize some basic thread information 
	 * The only flags we want to copy for now are
	 * the running-mode */
	Thread->Id = (UUId_t)Key.Value;
	Thread->ParentId = Parent->Id;
	Thread->AshId = PHOENIX_NO_ASH;
	Thread->Flags = (Flags & THREADING_MODEMASK);
	Thread->Function = Function;
	Thread->Args = Arguments;

	/* Setup initial scheduler information */
	Thread->Queue = -1;
	Thread->TimeSlice = MCORE_INITIAL_TIMESLICE;
	Thread->Priority = PriorityNormal;

	/* Create the pipe for communiciation */
	Thread->Pipe = PipeCreate(PIPE_DEFAULT_SIZE, 0);

	/* Flag-Special-Case:
	 * If we are CPU bound */
	if (Flags & THREADING_CPUBOUND) {
		Thread->CpuId = Cpu;
		Thread->Flags |= THREADING_CPUBOUND;
	}
	else {
		Thread->CpuId = 0xFF;	/* Select the low bearing CPU */
	}

	/* Flag-Special-Case
	 * If it's NOT a kernel thread
	 * we specify transition-mode */
	if (THREADING_RUNMODE(Flags) != THREADING_KERNELMODE) {
		Thread->Flags |= THREADING_SWITCHMODE;
	}

	/* Flag-Special-Case
	 * Determine the address space we want
	 * to initialize for this thread */
	if (THREADING_RUNMODE(Flags) == THREADING_KERNELMODE) {
		Thread->AddressSpace = AddressSpaceCreate(AS_TYPE_INHERIT);
	}
	else {
		Flags_t ASFlags = 0;

		if (THREADING_RUNMODE(Flags) == THREADING_DRIVERMODE) {
			ASFlags |= AS_TYPE_DRIVER;
		}
		else {
			ASFlags |= AS_TYPE_APPLICATION;
		}

		if (Flags & THREADING_INHERIT) {
			ASFlags |= AS_TYPE_INHERIT;
		}

		/* Create the address space */
		Thread->AddressSpace = AddressSpaceCreate(ASFlags);
	}

	/* Create thread-data 
	 * But use different entry point
	 * based upon usermode thread or kernel mode thread */
	if (THREADING_RUNMODE(Flags) == THREADING_KERNELMODE
		|| !(Flags & THREADING_INHERIT)) {
		Thread->ThreadData = IThreadCreate(THREADING_KERNELMODE, (Addr_t)&ThreadingEntryPoint);
	}
	else {
		Thread->ThreadData = IThreadCreate(THREADING_KERNELMODE, (Addr_t)&ThreadingEntryPointUserMode);
	}

	/* Append it to list & scheduler */
	Key.Value = (int)Thread->Id;
	ListAppend(GlbThreads, ListCreateNode(Key, Key, Thread));
	SchedulerReadyThread(Thread);

	/* Done */
	return Thread->Id;
}

/* ThreadingCleanupThread
 * Cleans up a thread and all it's resources, the
 * address space is not cleaned up untill all threads
 * in the given space has been shut down. Must be
 * called from a seperate thread */
void ThreadingCleanupThread(MCoreThread_t *Thread)
{
	/* Cleanup arch resources */
	AddressSpaceDestroy(Thread->AddressSpace);
	IThreadDestroy(Thread);

	/* Cleanup structure */
	PipeDestroy(Thread->Pipe);
	kfree((void*)Thread->Name);
	kfree(Thread);
}

/* ThreadingExitThread
 * Exits the current thread by marking it finished
 * and yielding control to scheduler */
void ThreadingExitThread(int ExitCode)
{
	/* Variables for setup */
	MCoreThread_t *Thread = NULL;
	UUId_t Cpu = 0;

	/* Retrieve the current cpu and
	* get the current thread */
	Cpu = CpuGetCurrentId();
	Thread = ThreadingGetCurrentThread(Cpu);

	/* Store exit code */
	Thread->RetCode = ExitCode;

	/* Mark thread finished */
	Thread->Flags |= THREADING_FINISHED;

	/* Wakeup people that were 
	 * waiting for the thread to finish */
	SchedulerWakeupAllThreads((Addr_t*)Thread);

	/* Yield control */
	IThreadYield();
}

/* ThreadingKillThread
 * Kills a thread with the given id, the thread
 * might not be killed immediately */
void ThreadingKillThread(UUId_t ThreadId)
{
	/* Get thread handle */
	MCoreThread_t *Target = ThreadingGetThread(ThreadId);

	/* Sanity 
	 * NULL check and IDLE check
	 * don't ever kill idle threads.. */
	if (Target == NULL
		|| (Target->Flags & THREADING_IDLE))
		return;

	/* Mark thread finished */
	Target->Flags |= THREADING_FINISHED;
	Target->RetCode = -1;

	/* Wakeup people that were
	* waiting for the thread to finish */
	SchedulerWakeupAllThreads((Addr_t*)Target);

	/* This means that it will be 
	 * cleaned up at next schedule */
}

/* ThreadingJoinThread
 * Can be used to wait for a thread the return 
 * value of this function is the ret-code of the thread */
int ThreadingJoinThread(UUId_t ThreadId)
{
	/* Get thread handle */
	MCoreThread_t *Target = ThreadingGetThread(ThreadId);

	/* Sanity */
	if (Target == NULL)
		return -1;

	/* Wait... */
	SchedulerSleepThread((Addr_t*)Target, 0);
	IThreadYield();

	/* When we reach this point 
	 * the thread is scheduled for
	 * destruction */
	return Target->RetCode;
}

/* ThreadingEnterUserMode
 * Initializes non-kernel mode and marks the thread
 * for transitioning, there is no return from this function */
void ThreadingEnterUserMode(void *AshInfo)
{
	/* Sensitive */
	MCoreAsh_t *Ash = (MCoreAsh_t*)AshInfo;
	IntStatus_t IntrState = InterruptDisable();
	MCoreThread_t *Thread = ThreadingGetCurrentThread(CpuGetCurrentId());

	/* Update this thread */
	Thread->AshId = Ash->Id;

	/* Only translate the argument address, since the pe
	 * loader already translates any image address */
	Thread->Function = (ThreadEntry_t)Ash->Executable->EntryAddress;
	Thread->Args = (void*)AddressSpaceTranslate(AddressSpaceGetCurrent(), MEMORY_LOCATION_RING3_ARGS);

	/* Underlying Call  */
	IThreadSetupUserMode(Thread, Ash->StackStart);

	/* This initiates the transition 
	 * nothing happpens before this */
	Thread->Flags |= THREADING_TRANSITION;

	/* Done! */
	InterruptRestoreState(IntrState);

	/* Yield control and a safe-ty catch */
	IThreadYield();
	for (;;);
}

/* ThreadingTerminateAshThreads
 * Marks all threads belonging to the given ashid
 * as finished and they will be cleaned up on next switch */
void ThreadingTerminateAshThreads(UUId_t AshId)
{
	/* Iterate thread list */
	foreach(tNode, GlbThreads) {
		MCoreThread_t *Thread = 
			(MCoreThread_t*)tNode->Data;

		/* Is it owned?
		 * Then we mark it finished */
		if (Thread->AshId == AshId) {
			Thread->Flags |= THREADING_FINISHED;
		}
	}
}

/* ThreadingGetCurrentThread
 * Retrieves the current thread on the given cpu
 * if there is any issues it returns NULL */
MCoreThread_t *ThreadingGetCurrentThread(UUId_t Cpu)
{
	/* Sanitize the current threading status */
	if (GlbThreadingEnabled != 1
		|| GlbCurrentThreads[Cpu] == NULL) {
		return NULL;
	}

	/* This, this is important */
	assert(GlbCurrentThreads[Cpu] != NULL);

	/* Get thread */
	return (MCoreThread_t*)GlbCurrentThreads[Cpu]->Data;
}

/* ThreadingGetCurrentThreadId
 * Retrives the current thread id on the current cpu
 * from the callers perspective */
UUId_t ThreadingGetCurrentThreadId(void)
{
	/* Get current cpu */
	UUId_t Cpu = CpuGetCurrentId();

	/* If it's during startup phase for cpu's
	 * we have to take precautions */
	if (GlbCurrentThreads[Cpu] == NULL) {
		return (UUId_t)Cpu;
	}

	/* Sanitize the threading status */
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
MCoreThread_t *ThreadingGetThread(UUId_t ThreadId)
{
	/* Iterate thread nodes and find the correct */
	foreach(tNode, GlbThreads) {
		MCoreThread_t *Thread = 
			(MCoreThread_t*)tNode->Data;

		/* Does id match? */
		if (Thread->Id == ThreadId) {
			return Thread;
		}
	}

	/* Damn, no match */
	return NULL;
}

/* ThreadingIsEnabled
 * Returns 1 if the threading system has been
 * initialized, otherwise it returns 0 */
int ThreadingIsEnabled(void)
{
	return GlbThreadingEnabled;
}

/* ThreadingIsCurrentTaskIdle
 * Is the given cpu running it's idle task? */
int ThreadingIsCurrentTaskIdle(UUId_t Cpu)
{
	/* Check the current threads flag */
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
Flags_t ThreadingGetCurrentMode(void)
{
	/* Sanitizie status first! */
	if (ThreadingIsEnabled() == 1) {
		return ThreadingGetCurrentThread(CpuGetCurrentId())->Flags & THREADING_MODEMASK;
	}
	else {
		return 0;
	}
}

/* ThreadingWakeCpu
 * Wake's the target cpu from an idle thread
 * by sending it an yield IPI */
void ThreadingWakeCpu(UUId_t Cpu)
{
	/* This is unfortunately arch-specific */
	IThreadWakeCpu(Cpu);
}

/* ThreadingReapZombies
 * Garbage-Collector function, it reaps and
 * cleans up all threads */
OsStatus_t ThreadingReap(void *UserData)
{
	// Instantiate the thread pointer
	MCoreThread_t *Thread = (MCoreThread_t*)UserData;

	// Call the cleanup
	ThreadingCleanupThread(Thread);

	// Done - no errors
	return OsNoError;
}

/* ThreadingDebugPrint
 * Prints out debugging information about each thread
 * in the system, only active threads */
void ThreadingDebugPrint(void)
{
	foreach(i, GlbThreads)
	{
		MCoreThread_t *t = (MCoreThread_t*)i->Data;
		LogDebug("THRD", "Thread %u (%s) - Flags %i, Queue %i, Timeslice %u, Cpu: %u",
			t->Id, t->Name, t->Flags, t->Queue, t->TimeSlice, t->CpuId);
	}
}

/* ThreadingSwitch
 * This is the thread-switch function and must be 
 * be called from the below architecture to get the
 * next thread to run */
MCoreThread_t *ThreadingSwitch(UUId_t Cpu, MCoreThread_t *Current, int PreEmptive)
{
	// Variables
	MCoreThread_t *NextThread = NULL;
	ListNode_t *Node = NULL;
	DataKey_t Key;

	// Get the node for the current thread
	Node = ThreadingGetCurrentNode(Cpu);

	/* Unless this one is done.. */
GetNextThread:
	if ((Current->Flags & THREADING_FINISHED) || (Current->Flags & THREADING_IDLE)
		|| (Current->Flags & THREADING_ENTER_SLEEP))
	{
		// If the thread is finished then add it to 
		// garbagecollector
		if (Current->Flags & THREADING_FINISHED) {
			SchedulerRemoveThread(Current);
			GcSignal(GlbThreadGcId, Current);
			ListRemoveByNode(GlbThreads, Node);
			ListDestroyNode(GlbThreads, Node);
		}

		// Handle the sleep flag
		if (Current->Flags & THREADING_ENTER_SLEEP) {
			Current->Flags &= ~(THREADING_ENTER_SLEEP);
		}
		
		/* Get next thread without scheduling the current */
		NextThread = SchedulerGetNextTask(Cpu, NULL, PreEmptive);
	}
	else
	{
		/* Yea we dont schedule idle tasks :-) */
		NextThread = SchedulerGetNextTask(Cpu, Current, PreEmptive);
	}

	/* Sanity */
	if (NextThread == NULL)
		NextThread = (MCoreThread_t*)GlbIdleThreads[Cpu]->Data;

	/* More sanity 
	 * If we have caught a finished thread that
	 * has been killed while scheduled, get a new */
	if (NextThread->Flags & THREADING_FINISHED) {
		Current = NextThread;
		goto GetNextThread;
	}

	/* Get node by thread */
	Key.Value = (int)NextThread->Id;
	Node = ListGetNodeByKey(GlbThreads, Key, 0);

	/* Update current */
	ThreadingUpdateCurrent(Cpu, Node);

	/* Done */
	return ThreadingGetCurrentThread(Cpu);
}
