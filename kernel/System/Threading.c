/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS X86-32 Threads
*/

/* Includes */
#include <Arch.h>
#include <assert.h>
#include <Scheduler.h>
#include <Threading.h>
#include <List.h>
#include <Heap.h>
#include <Mutex.h>
#include <string.h>
#include <stdio.h>

/* Globals */
list_t *GlbThreads = NULL;
list_t *GlbZombieThreads = NULL;
volatile TId_t GlbThreadId = 0;
volatile list_node_t *GlbCurrentThreads[64];
volatile list_node_t *GlbIdleThreads[64];
volatile int GlbThreadingEnabled = 0;
Mutex_t GlbThreadLock;

/* Initialization
* Creates the main thread */
void ThreadingInit(void)
{
	MCoreThread_t *Init;
	list_node_t *Node;
	uint32_t Itr = 0;

	/* Create threading list */
	GlbThreads = list_create(LIST_SAFE);
	GlbZombieThreads = list_create(LIST_SAFE);
	GlbThreadId = 0;

	/* Set all NULL */
	for (Itr = 0; Itr < 64; Itr++)
	{
		GlbCurrentThreads[Itr] = NULL;
		GlbIdleThreads[Itr] = NULL;
	}

	/* Setup initial thread */
	Init = (MCoreThread_t*)kmalloc(sizeof(MCoreThread_t));
	Init->Name = strdup("Idle");
	Init->Priority = 60;
	Init->Flags = THREADING_IDLE | THREADING_SYSTEMTHREAD | THREADING_CPUBOUND;
	Init->TimeSlice = MCORE_IDLE_TIMESLICE;
	Init->ParentId = 0xDEADBEEF;
	Init->ThreadId = GlbThreadId;
	Init->CpuId = 0;
	Init->Func = NULL;
	Init->Args = NULL;
	Init->SleepResource = NULL;

	/* Create Address Space */
	Init->AddrSpace = AddressSpaceCreate(ADDRESS_SPACE_KERNEL);

	/* Create thread-data */
	Init->ThreadData = _ThreadInitBoot();

	/* Reset lock */
	MutexConstruct(&GlbThreadLock);

	/* Create a node for the scheduler */
	Node = list_create_node(GlbThreadId, Init);
	GlbCurrentThreads[0] = Node;
	GlbIdleThreads[0] = Node;

	/* Create a node for the thread-list */
	Node = list_create_node(GlbThreadId, Init);
	list_append(GlbThreads, Node);

	/* Increase Id */
	GlbThreadId++;

	/* Enable */
	GlbThreadingEnabled = 1;
}

/* Initialises AP task */
void ThreadingApInit(Cpu_t Cpu)
{
	MCoreThread_t *Init;
	list_node_t *Node;

	/* Setup initial thread */
	Init = (MCoreThread_t*)kmalloc(sizeof(MCoreThread_t));
	Init->Name = strdup("ApIdle");
	Init->Priority = 60;
	Init->Flags = THREADING_IDLE | THREADING_SYSTEMTHREAD | THREADING_CPUBOUND;
	Init->TimeSlice = MCORE_IDLE_TIMESLICE;
	Init->ParentId = 0xDEADBEEF;
	Init->ThreadId = GlbThreadId;
	Init->CpuId = Cpu;
	Init->Func = NULL;
	Init->Args = NULL;
	Init->SleepResource = NULL;

	/* Create Address Space */
	Init->AddrSpace = AddressSpaceCreate(ADDRESS_SPACE_KERNEL);

	/* Create the threading data */
	Init->ThreadData = _ThreadInitAp();

	/* Create a node for the scheduler */
	Node = list_create_node(GlbThreadId, Init);
	GlbCurrentThreads[Cpu] = Node;
	GlbIdleThreads[Cpu] = Node;

	/* Create a node for the thread list */
	Node = list_create_node(GlbThreadId, Init);
	list_append(GlbThreads, Node);

	/* Increase Id */
	GlbThreadId++;
}

/* Get Current Thread */
MCoreThread_t *ThreadingGetCurrentThread(Cpu_t Cpu)
{
	/* Sanity */
	if (GlbThreadingEnabled != 1
		|| GlbCurrentThreads[Cpu] == NULL
		|| GlbCurrentThreads[Cpu]->data == NULL)
		return NULL;

	/* Get thread */
	return (MCoreThread_t*)GlbCurrentThreads[Cpu]->data;
}

/* Get Current Scheduler(!!!) Node */
list_node_t *ThreadingGetCurrentNode(Cpu_t Cpu)
{
	/* Get thread */
	return (list_node_t*)GlbCurrentThreads[Cpu];
}

/* Get current threading id */
TId_t ThreadingGetCurrentThreadId(void)
{
	Cpu_t Cpu = ApicGetCpu();

	/* If it's during startup phase for cpu's
	* we have to take precautions */
	if (GlbCurrentThreads[Cpu] == NULL)
		return (TId_t)Cpu;

	if (GlbThreadId == 0)
		return 0;
	else
		return ThreadingGetCurrentThread(Cpu)->ThreadId;
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
	_ThreadWakeUpCpu(Cpu);
}

/* Marks current thread for sleep */
void *ThreadingEnterSleep(void)
{
	/* Get current cpu */
	Cpu_t Cpu = ApicGetCpu();
	
	/* Get current thread */
	MCoreThread_t *cThread = ThreadingGetCurrentThread(Cpu);
	cThread->Flags |= THREAIDNG_ENTER_SLEEP;

	/* Return the node */
	return ThreadingGetCurrentNode(Cpu);
}

/* Set Current List Node */
void ThreadingUpdateCurrent(Cpu_t Cpu, list_node_t *Node)
{
	GlbCurrentThreads[Cpu] = Node;
}

/* Cleanup a thread */
void ThreadingCleanupThread(MCoreThread_t *Thread)
{
	_CRT_UNUSED(Thread);
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
		MCoreThread_t *t = (MCoreThread_t*)i->data;
		printf("Thread %u (%s) - Flags %u, Priority %u, Timeslice %u, Cpu: %u\n",
			t->ThreadId, t->Name, t->Flags, t->Priority, t->TimeSlice, t->CpuId);
	}
}

/* This is actually every thread entry point,
* It makes sure to handle ALL threads terminating */
void ThreadingEntryPoint(void)
{
	MCoreThread_t *cThread;
	Cpu_t Cpu;

	/* Get cpu */
	Cpu = ApicGetCpu();

	/* Get current thread */
	cThread = ThreadingGetCurrentThread(Cpu);

	/* Call entry point */
	cThread->Func(cThread->Args);

	/* IF WE REACH THIS POINT THREAD IS DONE! */
	cThread->Flags |= THREADING_FINISHED;

	/* Yield */
	_ThreadYield();

	/* Safety-Catch */
	for (;;);
}

/* Create a new thread */
TId_t ThreadingCreateThread(char *Name, ThreadEntry_t Function, void *Args, int Flags)
{
	MCoreThread_t *nThread, *tParent;
	Cpu_t Cpu;
	list_node_t *Node;

	/* Get mutex */
	MutexLock(&GlbThreadLock);

	/* Get cpu */
	Cpu = ApicGetCpu();
	tParent = ThreadingGetCurrentThread(Cpu);

	/* Allocate a new thread structure */
	nThread = (MCoreThread_t*)kmalloc(sizeof(MCoreThread_t));

	/* Setup */
	nThread->Name = strdup(Name);
	nThread->Func = Function;
	nThread->Args = Args;
	nThread->Flags = Flags;

	/* If we are CPU bound :/ */
	if (Flags & THREADING_CPUBOUND)
	{
		nThread->CpuId = Cpu;
	}
	else
	{
		/* Select the low bearing CPU */
		nThread->CpuId = 0xFF;
	}

	nThread->ParentId = tParent->ThreadId;
	nThread->ThreadId = GlbThreadId;
	nThread->SleepResource = NULL;

	/* Scheduler Related */
	nThread->Priority = -1;
	nThread->TimeSlice = MCORE_INITIAL_TIMESLICE;

	/* Create Address Space */
	nThread->AddrSpace = AddressSpaceCreate(ADDRESS_SPACE_INHERIT);

	/* Create thread-data */
	nThread->ThreadData = _ThreadInit((Addr_t)&ThreadingEntryPoint);

	/* Increase id */
	GlbThreadId++;

	/* Release lock */
	MutexUnlock(&GlbThreadLock);

	/* Append it to list & scheduler */
	Node = list_create_node(nThread->ThreadId, nThread);
	list_append(GlbThreads, Node);

	/* Ready */
	Node = list_create_node(nThread->ThreadId, nThread);
	SchedulerReadyThread(Node);

	/* Done */
	return nThread->ThreadId;
}

/* Handles and switches thread from the current */
MCoreThread_t *ThreadingSwitch(Cpu_t Cpu, MCoreThread_t *Current, uint8_t PreEmptive)
{
	/* We'll need these */
	list_node_t *node;

	/* Get a new task! */
	node = ThreadingGetCurrentNode(Cpu);

	/* Unless this one is done.. */
	if (Current->Flags & THREADING_FINISHED || Current->Flags & THREADING_IDLE
		|| Current->Flags & THREAIDNG_ENTER_SLEEP)
	{
		/* Someone should really kill those zombies :/ */
		if (Current->Flags & THREADING_FINISHED)
		{
			/* Get thread node */
			list_node_t *n = list_get_data_by_id(GlbThreads, Current->ThreadId, 0);

			/* Remove it */
			list_remove_by_node(GlbThreads, n);

			/* Append to reaper list */
			list_append(GlbZombieThreads, n);
		}

		/* Remove flag so it does not happen again */
		if (Current->Flags & THREAIDNG_ENTER_SLEEP)
			Current->Flags &= ~(THREAIDNG_ENTER_SLEEP);

		node = SchedulerGetNextTask(Cpu, NULL, PreEmptive);
	}
	else
	{
		/* Yea we dont schedule idle tasks :-) */
		node = SchedulerGetNextTask(Cpu, node, PreEmptive);
	}

	/* Sanity */
	if (node == NULL)
		node = (list_node_t*)GlbIdleThreads[Cpu];

	/* Update current */
	ThreadingUpdateCurrent(Cpu, node);

	/* Done */
	return ThreadingGetCurrentThread(Cpu);
}