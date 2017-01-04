/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS Threads
*/

/* Includes */
#include <Arch.h>
#include <Scheduler.h>
#include <Threading.h>
#include <Modules/Phoenix.h>
#include <Heap.h>
#include <Log.h>
#include <CriticalSection.h>

/* C-Library */
#include <assert.h>
#include <ds/list.h>
#include <string.h>
#include <stdio.h>

/* Globals */
List_t *GlbThreads = NULL;
List_t *GlbZombieThreads = NULL;
ThreadId_t GlbThreadId = 0;
ListNode_t *GlbCurrentThreads[64];
ListNode_t *GlbIdleThreads[64];
int GlbThreadingEnabled = 0;
CriticalSection_t GlbThreadLock;

/* Initialization
* Creates the main thread */
void ThreadingInit(void)
{
	/* Vars */
	MCoreThread_t *Init;
	ListNode_t *Node;
	DataKey_t Key;
	int Itr = 0;

	/* Create threading list */
	GlbThreads = ListCreate(KeyInteger, LIST_SAFE);
	GlbZombieThreads = ListCreate(KeyInteger, LIST_SAFE);
	GlbThreadId = 0;

	/* Set all NULL */
	for (Itr = 0; Itr < 64; Itr++)
	{
		GlbCurrentThreads[Itr] = NULL;
		GlbIdleThreads[Itr] = NULL;
	}

	/* Setup initial thread */
	Init = (MCoreThread_t*)kmalloc(sizeof(MCoreThread_t));
	memset(Init, 0, sizeof(MCoreThread_t));

	Init->Name = strdup("Idle");
	Init->Queue = MCORE_SCHEDULER_LEVELS - 1;
	Init->Flags = THREADING_IDLE | THREADING_SYSTEMTHREAD | THREADING_CPUBOUND;
	Init->TimeSlice = MCORE_IDLE_TIMESLICE;
	Init->Priority = PriorityLow;
	Init->ParentId = 0xDEADBEEF;
	Init->Id = GlbThreadId;
	Init->AshId = PHOENIX_NO_ASH;

	/* Create Address Space */
	Init->AddressSpace = AddressSpaceCreate(ADDRESS_SPACE_KERNEL);

	/* Create thread-data */
	Init->ThreadData = IThreadInitBoot();

	/* Reset lock */
	CriticalSectionConstruct(&GlbThreadLock, CRITICALSECTION_PLAIN);

	/* Create a list node */
	Key.Value = (int)GlbThreadId;
	Node = ListCreateNode(Key, Key, Init);
	GlbCurrentThreads[0] = Node;
	GlbIdleThreads[0] = Node;

	/* append to thread list */
	ListAppend(GlbThreads, Node);

	/* Increase Id */
	GlbThreadId++;

	/* Enable */
	GlbThreadingEnabled = 1;
}

/* Initialises AP task */
void ThreadingApInit(Cpu_t Cpu)
{
	/* Vars */
	MCoreThread_t *Init;
	ListNode_t *Node;
	DataKey_t Key;

	/* Setup initial thread */
	Init = (MCoreThread_t*)kmalloc(sizeof(MCoreThread_t));
	memset(Init, 0, sizeof(MCoreThread_t));

	Init->Name = strdup("ApIdle");
	Init->Queue = MCORE_SCHEDULER_LEVELS - 1;
	Init->Flags = THREADING_IDLE | THREADING_SYSTEMTHREAD | THREADING_CPUBOUND;
	Init->TimeSlice = MCORE_IDLE_TIMESLICE;
	Init->Priority = PriorityLow;
	Init->ParentId = 0xDEADBEEF;
	Init->Id = GlbThreadId;
	Init->CpuId = Cpu;
	Init->AshId = PHOENIX_NO_ASH;

	/* Create Address Space */
	Init->AddressSpace = AddressSpaceCreate(ADDRESS_SPACE_KERNEL);

	/* Create the threading data */
	Init->ThreadData = IThreadInitAp();

	/* Create a node for the scheduler */
	Key.Value = (int)GlbThreadId;
	Node = ListCreateNode(Key, Key, Init);
	GlbCurrentThreads[Cpu] = Node;
	GlbIdleThreads[Cpu] = Node;

	/* Append to thread list */
	ListAppend(GlbThreads, Node);

	/* Increase Id */
	GlbThreadId++;
}

/* Get Current Thread */
MCoreThread_t *ThreadingGetCurrentThread(Cpu_t Cpu)
{
	/* Sanity */
	if (GlbThreadingEnabled != 1
		|| GlbCurrentThreads[Cpu] == NULL)
		return NULL;

	/* This, this is important */
	assert(GlbCurrentThreads[Cpu] != NULL);

	/* Get thread */
	return (MCoreThread_t*)GlbCurrentThreads[Cpu]->Data;
}

/* Get Current Scheduler(!!!) Node */
ListNode_t *ThreadingGetCurrentNode(Cpu_t Cpu) {
	return GlbCurrentThreads[Cpu];
}

/* Get current threading id */
ThreadId_t ThreadingGetCurrentThreadId(void)
{
	/* Get current cpu */
	Cpu_t Cpu = ApicGetCpu();

	/* If it's during startup phase for cpu's
	* we have to take precautions */
	if (GlbCurrentThreads[Cpu] == NULL)
		return (ThreadId_t)Cpu;

	if (GlbThreadId == 0)
		return 0;
	else
		return ThreadingGetCurrentThread(Cpu)->Id;
}

/* Lookup thread by the given 
 * thread-id, returns NULL if invalid */
MCoreThread_t *ThreadingGetThread(ThreadId_t ThreadId)
{
	/* Iterate thread nodes 
	 * and find the correct */
	foreach(tNode, GlbThreads)
	{
		/* Cast */
		MCoreThread_t *Thread = (MCoreThread_t*)tNode->Data;

		/* Check */
		if (Thread->Id == ThreadId)
			return Thread;
	}

	/* Damn, no match */
	return NULL;
}

/* Is current thread idle task? */
int ThreadingIsCurrentTaskIdle(Cpu_t Cpu)
{
	/* Has flag? */
	if (ThreadingGetCurrentThread(Cpu)->Flags & THREADING_IDLE)
		return 1;
	else
		return 0;
}

/* Wake's up CPU */
void ThreadingWakeCpu(Cpu_t Cpu)
{
	/* This is unfortunately arch-specific */
	IThreadWakeCpu(Cpu);
}

/* Set Current List Node */
void ThreadingUpdateCurrent(Cpu_t Cpu, ListNode_t *Node) {
	GlbCurrentThreads[Cpu] = Node;
}

/* Cleanup a thread */
void ThreadingCleanupThread(MCoreThread_t *Thread)
{
	/* Cleanup arch resources */
	AddressSpaceDestroy(Thread->AddressSpace);
	IThreadDestroy(Thread->ThreadData);

	/* Cleanup structure */
	kfree((void*)Thread->Name);
	kfree(Thread);
}

/* Cleans up all threads */
void ThreadingReapZombies(void)
{
	/* Reap untill list is empty */
	ListNode_t *tNode = ListPopFront(GlbZombieThreads);

	while (tNode != NULL)
	{
		/* Cast */
		MCoreThread_t *Thread = (MCoreThread_t*)tNode->Data;

		/* Clean it up */
		ThreadingCleanupThread(Thread);

		/* Clean up rest */
		kfree(tNode);

		/* Get next node */
		tNode = ListPopFront(GlbZombieThreads);
	}
}

/* Is threading running? */
int ThreadingIsEnabled(void)
{
	return GlbThreadingEnabled;
}

/* Prints threads */
void ThreadingDebugPrint(void)
{
	foreach(i, GlbThreads)
	{
		MCoreThread_t *t = (MCoreThread_t*)i->Data;
		printf("Thread %u (%s) - Flags %i, Queue %i, Timeslice %u, Cpu: %u\n",
			t->Id, t->Name, t->Flags, t->Queue, t->TimeSlice, t->CpuId);
	}
}

/* This is actually every thread entry point,
 * It makes sure to handle ALL threads terminating */
void ThreadingEntryPoint(void)
{
	/* Vars */
	MCoreThread_t *cThread;
	Cpu_t Cpu;

	/* Get cpu */
	Cpu = ApicGetCpu();

	/* Get current thread */
	cThread = ThreadingGetCurrentThread(Cpu);

	/* Call entry point */
	cThread->Function(cThread->Args);

	/* IF WE REACH THIS POINT THREAD IS DONE! */
	cThread->Flags |= THREADING_FINISHED;

	/* Yield */
	IThreadYield();

	/* Safety-Catch */
	for (;;);
}

/* This is the userspace version of the entry point
 * and is used for initializing userspace threads 
 * Threads started like this MUST use ThreadingExitThread */
void ThreadingEntryPointUserMode(void)
{
	/* Sensitive */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreThread_t *cThread = ThreadingGetCurrentThread(CurrentCpu);

	/* It's important to create 
	 * and map the stack before setting up */
	Addr_t BaseAddress = ((MEMORY_LOCATION_USER_STACK - 0x1) & PAGE_MASK);
	AddressSpaceMap(AddressSpaceGetCurrent(), 
		BaseAddress, ASH_STACK_INIT, MEMORY_MASK_DEFAULT, ADDRESS_SPACE_FLAG_USER);
	BaseAddress += (MEMORY_LOCATION_USER_STACK & ~(PAGE_MASK));

	/* Underlying Call */
	IThreadInitUserMode(cThread->ThreadData, BaseAddress,
		(Addr_t)cThread->Function, (Addr_t)cThread->Args);

	/* Update this thread */
	cThread->Flags |= THREADING_TRANSITION;

	/* Yield */
	IThreadYield();

	/* Safety-Catch */
	for (;;);
}

/* Create a new thread with the given name,
 * entry point, arguments and flags, if name 
 * is null, a generic name will be generated 
 * Thread is started as soon as possible */
ThreadId_t ThreadingCreateThread(char *Name, ThreadEntry_t Function, void *Args, int Flags)
{
	/* Vars */
	MCoreThread_t *nThread, *tParent;
	char TmpBuffer[16];
	DataKey_t Key;
	Cpu_t Cpu;

	/* Get mutex */
	CriticalSectionEnter(&GlbThreadLock);

	/* Get cpu */
	Cpu = ApicGetCpu();
	tParent = ThreadingGetCurrentThread(Cpu);

	/* Allocate a new thread structure */
	nThread = (MCoreThread_t*)kmalloc(sizeof(MCoreThread_t));
	memset(nThread, 0, sizeof(MCoreThread_t));

	/* Sanitize name */
	if (Name == NULL) {
		memset(&TmpBuffer[0], 0, sizeof(TmpBuffer));
		sprintf(&TmpBuffer[0], "Thread %u", GlbThreadId);
		nThread->Name = strdup(&TmpBuffer[0]);
	}
	else {
		nThread->Name = strdup(Name);
	}

	/* Setup */
	nThread->Function = Function;
	nThread->Args = Args;

	/* If we are CPU bound :/ */
	if (Flags & THREADING_CPUBOUND) {
		nThread->CpuId = Cpu;
		nThread->Flags |= THREADING_CPUBOUND;
	}
	else
	{
		/* Select the low bearing CPU */
		nThread->CpuId = 0xFF;
	}

	nThread->Id = GlbThreadId;
	nThread->ParentId = tParent->Id;
	nThread->AshId = PHOENIX_NO_ASH;

	/* Scheduler Related */
	nThread->Queue = -1;
	nThread->TimeSlice = MCORE_INITIAL_TIMESLICE;
	nThread->Priority = PriorityNormal;

	/* Create Address Space */
	if (Flags & THREADING_USERMODE) {
		if (Flags & THREADING_INHERIT)
			nThread->AddressSpace = AddressSpaceCreate(ADDRESS_SPACE_USER | ADDRESS_SPACE_INHERIT);
		else
			nThread->AddressSpace = AddressSpaceCreate(ADDRESS_SPACE_USER);
	}
	else
		nThread->AddressSpace = AddressSpaceCreate(ADDRESS_SPACE_INHERIT);

	/* Create thread-data 
	 * But use different entry point
	 * based upon usermode thread or
	 * kernel mode thread */
	if ((Flags & (THREADING_USERMODE | THREADING_INHERIT))
		== (THREADING_USERMODE | THREADING_INHERIT)) {
		nThread->ThreadData = IThreadInit((Addr_t)&ThreadingEntryPointUserMode);
	}
	else {
		nThread->ThreadData = IThreadInit((Addr_t)&ThreadingEntryPoint);
	}

	/* Increase id */
	GlbThreadId++;

	/* Release lock */
	CriticalSectionLeave(&GlbThreadLock);

	/* Append it to list & scheduler */
	Key.Value = (int)nThread->Id;
	ListAppend(GlbThreads, ListCreateNode(Key, Key, nThread));
	SchedulerReadyThread(nThread);

	/* Done */
	return nThread->Id;
}

/* Exits the current thread by marking it finished
 * and yielding control to scheduler */
void ThreadingExitThread(int ExitCode)
{
	/* Get current thread handle */
	MCoreThread_t *Current = ThreadingGetCurrentThread(ApicGetCpu());

	/* Store exit code */
	Current->RetCode = ExitCode;

	/* Mark thread finished */
	Current->Flags |= THREADING_FINISHED;

	/* Wakeup people that were 
	 * waiting for the thread to finish */
	SchedulerWakeupAllThreads((Addr_t*)Current);

	/* Yield control */
	IThreadYield();
}

/* Kills a thread with the given id 
 * this force-kills the thread, thread
 * might not be killed immediately */
void ThreadingKillThread(ThreadId_t ThreadId)
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

/* Can be used to wait for a thread 
 * the return value of this function
 * is the ret-code of the thread */
int ThreadingJoinThread(ThreadId_t ThreadId)
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

/* Enters Usermode */
void ThreadingEnterUserMode(void *AshInfo)
{
	/* Sensitive */
	MCoreAsh_t *Ash = (MCoreAsh_t*)AshInfo;
	IntStatus_t IntrState = InterruptDisable();
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreThread_t *cThread = ThreadingGetCurrentThread(CurrentCpu);

	/* Update this thread */
	cThread->AshId = Ash->Id;

	/* Underlying Call  */
	IThreadInitUserMode(cThread->ThreadData, Ash->StackStart,
		Ash->Executable->EntryAddress, MEMORY_LOCATION_USER_ARGS);

	/* This initiates the transition 
	 * nothing happpens before this */
	cThread->Flags |= THREADING_TRANSITION;

	/* Done! */
	InterruptRestoreState(IntrState);
}

/* End all threads by process id */
void ThreadingTerminateAshThreads(PhxId_t AshId)
{
	/* Iterate thread list */
	foreach(tNode, GlbThreads)
	{
		/* Cast */
		MCoreThread_t *Thread = (MCoreThread_t*)tNode->Data;

		/* Is it owned?
		 * Then we mark it finished */
		if (Thread->AshId == AshId) {
			Thread->Flags |= THREADING_FINISHED;
		}
	}
}

/* Handles and switches thread from the current */
MCoreThread_t *ThreadingSwitch(Cpu_t Cpu, MCoreThread_t *Current, uint8_t PreEmptive)
{
	/* We'll need these */
	MCoreThread_t *NextThread;
	ListNode_t *Node;
	DataKey_t Key;

	/* Get a new task! */
	Node = ThreadingGetCurrentNode(Cpu);

	/* Unless this one is done.. */
GetNextThread:
	if ((Current->Flags & THREADING_FINISHED) || (Current->Flags & THREADING_IDLE)
		|| (Current->Flags & THREADING_ENTER_SLEEP))
	{
		/* Someone should really kill those zombies :/ */
		if (Current->Flags & THREADING_FINISHED)
		{
			/* Deschedule it */
			SchedulerRemoveThread(Current);

			/* Remove it */
			ListRemoveByNode(GlbThreads, Node);

			/* Append to reaper list */
			ListAppend(GlbZombieThreads, Node);
		}

		/* Remove flag so it does not happen again */
		if (Current->Flags & THREADING_ENTER_SLEEP)
			Current->Flags &= ~(THREADING_ENTER_SLEEP);

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
