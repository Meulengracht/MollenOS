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
#include <Thread.h>
#include <Memory.h>
#include <List.h>
#include <LApic.h>
#include <Heap.h>
#include <Gdt.h>
#include <Mutex.h>
#include <string.h>
#include <stdio.h>

/* Globals */
list_t *GlbThreads = NULL;
list_t *GlbZombieThreads = NULL;
volatile TId_t GlbThreadId = 0;
volatile list_node_t *GlbCurrentThreads[64];
volatile list_node_t *GlbIdleThreads[64];
volatile uint8_t GlbThreadingEnabled = 0;
Mutex_t GlbThreadLock;

/* Externs */
extern volatile uint32_t GlbTimerQuantum;
extern uint32_t memory_get_cr3(void);
extern void save_fpu(Addr_t *buffer);
extern void set_ts(void);
extern void _yield(void);
extern void enter_thread(Registers_t *regs);

/* The YIELD handler */
int ThreadingYield(void *Args)
{
	/* Get registers */
	Registers_t *Regs = NULL;
	uint32_t TimeSlice = 20;
	uint32_t TaskPriority = 0;
	Cpu_t CurrCpu = ApicGetCpu();

	/* Send EOI */
	ApicSendEoi(0, INTERRUPT_YIELD);

	/* Switch Task */ 
	Regs = (void*)ThreadingSwitch((Registers_t*)Args, 0, &TimeSlice, &TaskPriority);

	/* If we just got hold of idle task, well fuck it disable timer
	* untill we get another task */
	if (!(ThreadingGetCurrentThread(CurrCpu)->Flags & X86_THREAD_IDLE))
	{
		/* Set Task Priority */
		ApicSetTaskPriority(61 - TaskPriority);

		/* Reset Timer Tick */
		ApicWriteLocal(LAPIC_INITIAL_COUNT, GlbTimerQuantum * TimeSlice);

		/* Re-enable timer in one-shot mode */
		ApicWriteLocal(LAPIC_TIMER_VECTOR, INTERRUPT_TIMER);		//0x20000 - Periodic
	}
	else
	{
		ApicWriteLocal(LAPIC_TIMER_VECTOR, 0x10000);
		ApicSetTaskPriority(0);
	}

	/* Enter new thread */
	enter_thread(Regs);

	/* Never reached */
	return X86_IRQ_HANDLED;
}

/* Initialization 
 * Creates the main thread */
void ThreadingInit(void)
{
	Thread_t *init;
	list_node_t *node;

	/* Create threading list */
	GlbThreads = list_create(LIST_SAFE);
	GlbZombieThreads = list_create(LIST_SAFE);
	GlbThreadId = 0;

	/* Setup initial thread */
	init = (Thread_t*)kmalloc(sizeof(Thread_t));
	init->Name = strdup("Idle");
	init->FpuBuffer = kmalloc_a(0x1000);
	init->Priority = 60;
	init->Flags = X86_THREAD_FPU_INITIALISED | X86_THREAD_USEDFPU | X86_THREAD_CPU_BOUND | X86_THREAD_IDLE;
	init->TimeSlice = MCORE_IDLE_TIMESLICE;
	init->ParentId = 0xDEADBEEF;
	init->ThreadId = GlbThreadId;
	init->CpuId = 0;
	init->Context = NULL;
	init->UserContext = NULL;
	init->Cr3 = memory_get_cr3();
	init->PageDirectory = MmVirtualGetCurrentDirectory(0);
	init->Func = NULL;
	init->Args = NULL;
	init->SleepResource = NULL;

	/* Reset lock */
	MutexConstruct(&GlbThreadLock);

	/* Memset the buffer */
	memset(init->FpuBuffer, 0, 0x1000);

	/* Create a node for the scheduler */
	node = list_create_node(GlbThreadId, init);
	GlbCurrentThreads[0] = node;
	GlbIdleThreads[0] = node;

	/* Create a node for the thread-list */
	node = list_create_node(GlbThreadId, init);
	list_append(GlbThreads, node);

	/* Increase Id */
	GlbThreadId++;

	/* Install Yield */
	InterruptInstallIdtOnly(0xFFFFFFFF, INTERRUPT_YIELD, ThreadingYield, NULL);

	/* Enable */
	GlbThreadingEnabled = 1;
}

/* Initialises AP task */
void ThreadingApInit(void)
{
	Cpu_t cpu;
	Thread_t *init;
	list_node_t *node;

	/* Setup initial thread */
	cpu = ApicGetCpu();
	init = (Thread_t*)kmalloc(sizeof(Thread_t));
	init->Name = strdup("ApIdle");
	init->FpuBuffer = kmalloc_a(0x1000);
	init->Priority = 60;
	init->Flags = X86_THREAD_FPU_INITIALISED | X86_THREAD_USEDFPU | X86_THREAD_CPU_BOUND | X86_THREAD_IDLE;
	init->TimeSlice = MCORE_IDLE_TIMESLICE;
	init->ParentId = 0xDEADBEEF;
	init->ThreadId = GlbThreadId;
	init->CpuId = cpu;
	init->Context = NULL;
	init->UserContext = NULL;
	init->Cr3 = memory_get_cr3(); 
	init->PageDirectory = MmVirtualGetCurrentDirectory(cpu);
	init->Func = NULL;
	init->Args = NULL;
	init->SleepResource = NULL;

	/* Memset the buffer */
	memset(init->FpuBuffer, 0, 0x1000);

	/* Create a node for the scheduler */
	node = list_create_node(GlbThreadId, init);
	GlbCurrentThreads[cpu] = node;
	GlbIdleThreads[cpu] = node;

	/* Create a node for the thread list */
	node = list_create_node(GlbThreadId, init);
	list_append(GlbThreads, node);

	/* Increase Id */
	GlbThreadId++;
}

/* Get Current Thread */
Thread_t *ThreadingGetCurrentThread(Cpu_t cpu)
{
	/* Get thread */
	return (Thread_t*)GlbCurrentThreads[cpu]->data;
}

/* Get Current Scheduler(!!!) Node */
list_node_t *ThreadingGetCurrentNode(Cpu_t cpu)
{
	/* Get thread */
	return (list_node_t*)GlbCurrentThreads[cpu];
}

TId_t ThreadingGetCurrentThreadId(void)
{
	Cpu_t cpu = ApicGetCpu();

	/* If it's during startup phase for cpu's 
	 * we have to take precautions */
	if (GlbCurrentThreads[cpu] == NULL)
		return (TId_t)cpu;

	if (GlbThreadId == 0)
		return 0;
	else
		return ThreadingGetCurrentThread(cpu)->ThreadId;
}

/* Is current thread idle task? */
int ThreadingIsCurrentTaskIdle(Cpu_t cpu)
{
	/* Has flag? */
	if (ThreadingGetCurrentThread(cpu)->Flags & X86_THREAD_IDLE)
		return 1;
	else
		return 0;
}

/* Wake's up CPU */
void ThreadingWakeCpu(Cpu_t Cpu)
{
	/* Are we on this cpu? */
	if (Cpu == ApicGetCpu())
	{
		/* Reset Timer Tick */
		ApicWriteLocal(LAPIC_INITIAL_COUNT, GlbTimerQuantum);

		/* Re-enable timer in one-shot mode */
		ApicWriteLocal(LAPIC_TIMER_VECTOR, INTERRUPT_TIMER);
	}
	else
		ApicSendIpi((uint8_t)Cpu, INTERRUPT_YIELD);
}

/* Marks current thread for sleep */
void *ThreadingEnterSleep(void)
{
	Cpu_t cpu = ApicGetCpu();
	Thread_t *t = ThreadingGetCurrentThread(cpu);
	t->Flags |= X86_THREAD_ENTER_SLEEP;
	return ThreadingGetCurrentNode(cpu);
}

/* Set Current List Node */
void ThreadingUpdateCurrent(Cpu_t cpu, list_node_t *Node)
{
	GlbCurrentThreads[cpu] = Node;
}

/* Cleanup a thread */
void ThreadingCleanupThread(Thread_t *Thread)
{
	_CRT_UNUSED(Thread);
}

/* Prints threads */
void ThreadingDebugPrint(void)
{
	foreach(i, GlbThreads)
	{
		Thread_t *t = (Thread_t*)i->data;
		printf("Thread %u (%s) - Flags %u, Priority %u, Timeslice %u, Cpu: %u\n",
			t->ThreadId, t->Name, t->Flags, t->Priority, t->TimeSlice, t->CpuId);
	}
}

/* This is actually every thread entry point, 
 * It makes sure to handle ALL threads terminating */
void ThreadingEntryPoint(void)
{
	Thread_t *t;
	Cpu_t cpu;

	/* Get cpu */
	cpu = ApicGetCpu();

	/* Get current thread */
	t = ThreadingGetCurrentThread(cpu);

	/* Call entry point */
	t->Func(t->Args);

	/* IF WE REACH THIS POINT THREAD IS DONE! */
	t->Flags |= X86_THREAD_FINISHED;

	/* Yield */
	_yield();

	/* Safety-Catch */
	for (;;);
}

/* Create a new thread */
TId_t ThreadingCreateThread(char *Name, ThreadEntry_t Function, void *Args, int Flags)
{
	Thread_t *t, *parent;
	Cpu_t cpu;
	list_node_t *node;

	/* Get mutex */
	MutexLock(&GlbThreadLock);

	/* Get cpu */
	cpu = ApicGetCpu();
	parent = ThreadingGetCurrentThread(cpu);

	/* Allocate a new thread structure */
	t = (Thread_t*)kmalloc(sizeof(Thread_t));

	/* Setup */
	t->Name = strdup(Name);
	t->Func = Function;
	t->Args = Args;
	t->Context = ContextCreate((Addr_t)ThreadingEntryPoint);
	t->UserContext = NULL;

	/* If we are CPU bound :/ */
	if (Flags & THREADING_CPUBOUND)
	{
		t->Flags = X86_THREAD_CPU_BOUND;
		t->CpuId = cpu;
	}
	else
	{
		/* Select the low bearing CPU */
		t->Flags = 0;
		t->CpuId = 0xFF;
	}

	t->ParentId = parent->ThreadId;
	t->ThreadId = GlbThreadId;
	t->SleepResource = NULL;

	/* Scheduler Related */
	t->Priority = -1;
	t->TimeSlice = MCORE_INITIAL_TIMESLICE;
	
	/* Memory */
	t->Cr3 = memory_get_cr3(); 
	t->PageDirectory = MmVirtualGetCurrentDirectory(cpu);

	/* FPU */
	t->FpuBuffer = (Addr_t*)kmalloc_a(0x1000);

	/* Memset the buffer */
	memset(t->FpuBuffer, 0, 0x1000);

	/* Increase id */
	GlbThreadId++;

	/* Release lock */
	MutexUnlock(&GlbThreadLock);

	/* Append it to list & scheduler */
	node = list_create_node(t->ThreadId, t);
	list_append(GlbThreads, node);

	/* Ready */
	node = list_create_node(t->ThreadId, t);
	SchedulerReadyThread(node);

	return t->ThreadId;
}

/* Task Switch occurs here */
Registers_t *ThreadingSwitch(Registers_t *Regs, int PreEmptive, uint32_t *TimeSlice, 
							 uint32_t *TaskPriority)
{
	Cpu_t cpu;
	Thread_t *t;
	list_node_t *node;

	/* Sanity */
	if (GlbThreadingEnabled == 0)
		return Regs;

	/* Get CPU */
	cpu = ApicGetCpu();

	/* Get thread */
	t = ThreadingGetCurrentThread(cpu);

	/* What the fuck?? */
	assert(t != NULL && Regs != NULL);

	/* Save FPU/MMX/SSE State */
	if (t->Flags & X86_THREAD_USEDFPU)
		save_fpu(t->FpuBuffer);

	/* Save stack */
	if (t->Flags & X86_THREAD_USERMODE)
		t->UserContext = Regs;
	else
		t->Context = Regs;

	/* Get a new task! */
	node = ThreadingGetCurrentNode(cpu);

	/* Unless this one is done.. */
	if (t->Flags & X86_THREAD_FINISHED || t->Flags & X86_THREAD_IDLE
		|| t->Flags & X86_THREAD_ENTER_SLEEP)
	{
		/* Someone should really kill those zombies :/ */
		if (t->Flags & X86_THREAD_FINISHED)
		{
			/* Get thread node */
			list_node_t *n = list_get_data_by_id(GlbThreads, t->ThreadId, 0);
			
			/* Remove it */
			list_remove_by_node(GlbThreads, n);

			/* Append to reaper list */
			list_append(GlbZombieThreads, n);
		}

		/* Remove flag so it does not happen again */
		if (t->Flags & X86_THREAD_ENTER_SLEEP)
			t->Flags &= ~(X86_THREAD_ENTER_SLEEP);

		node = SchedulerGetNextTask(cpu, NULL, PreEmptive);
	}
	else
	{
		/* Yea we dont schedule idle tasks :-) */
		node = SchedulerGetNextTask(cpu, node, PreEmptive);
	}

	/* Sanity */
	if (node == NULL)
		node = (list_node_t*)GlbIdleThreads[cpu];

	/* Update current */
	ThreadingUpdateCurrent(cpu, node);
	t = ThreadingGetCurrentThread(cpu);

	/* Update user variables */
	*TimeSlice = t->TimeSlice;
	*TaskPriority = t->Priority;

	/* Update Addressing Space */
	MmVirtualSwitchPageDirectory(cpu, (PageDirectory_t*)t->PageDirectory, t->Cr3);

	/* Set TSS */
	TssUpdateStack(cpu, (Addr_t)t->Context);

	/* Finish Transition */
	if (t->Flags & X86_THREAD_TRANSITION)
	{
		t->Flags &= ~X86_THREAD_TRANSITION;
		t->Flags |= X86_THREAD_USERMODE;
	}

	/* Clear FPU/MMX/SSE */
	t->Flags &= ~X86_THREAD_USEDFPU;

	/* Set TS bit in CR0 */
	set_ts();

	/* Return new stack */
	if (t->Flags & X86_THREAD_USERMODE)
		return t->UserContext;
	else
		return t->Context;
}