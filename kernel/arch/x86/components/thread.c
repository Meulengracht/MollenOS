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
#include <Threading.h>
#include <Thread.h>
#include <Memory.h>
#include <List.h>
#include <Heap.h>
#include <Apic.h>
#include <Gdt.h>
#include <string.h>
#include <stdio.h>

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
	Regs = (void*)_ThreadingSwitch((Registers_t*)Args, 0, &TimeSlice, &TaskPriority);

	/* If we just got hold of idle task, well fuck it disable timer
	* untill we get another task */
	if (!(ThreadingGetCurrentThread(CurrCpu)->Flags & THREADING_IDLE))
	{
		/* Set Task Priority */
		ApicSetTaskPriority(61 - TaskPriority);

		/* Reset Timer Tick */
		ApicWriteLocal(APIC_INITIAL_COUNT, GlbTimerQuantum * TimeSlice);

		/* Re-enable timer in one-shot mode */
		ApicWriteLocal(APIC_TIMER_VECTOR, INTERRUPT_TIMER);		//0x20000 - Periodic
	}
	else
	{
		ApicWriteLocal(APIC_TIMER_VECTOR, 0x10000);
		ApicSetTaskPriority(0);
	}

	/* Enter new thread */
	enter_thread(Regs);

	/* Never reached */
	return X86_IRQ_HANDLED;
}

/* Initialization 
 * Creates the main thread */
x86Thread_t *_ThreadInitBoot(void)
{
	x86Thread_t *Init;

	/* Setup initial thread */
	Init = (x86Thread_t*)kmalloc(sizeof(x86Thread_t));
	Init->FpuBuffer = kmalloc_a(0x1000);
	Init->Flags = X86_THREAD_FPU_INITIALISED | X86_THREAD_USEDFPU;
	Init->Context = NULL;
	Init->UserContext = NULL;
	Init->Cr3 = memory_get_cr3();
	Init->PageDirectory = MmVirtualGetCurrentDirectory(0);

	/* Memset the buffer */
	memset(Init->FpuBuffer, 0, 0x1000);

	/* Install Yield */
	InterruptInstallIdtOnly(0xFFFFFFFF, INTERRUPT_YIELD, ThreadingYield, NULL);

	/* Done */
	return Init;
}

/* Initialises AP task */
x86Thread_t *_ThreadInitAp(Cpu_t Cpu)
{
	x86Thread_t *Init;

	/* Setup initial thread */
	Init = (x86Thread_t*)kmalloc(sizeof(x86Thread_t));
	Init->FpuBuffer = kmalloc_a(0x1000);
	Init->Flags = X86_THREAD_FPU_INITIALISED | X86_THREAD_USEDFPU;
	Init->Context = NULL;
	Init->UserContext = NULL;
	Init->Cr3 = memory_get_cr3();
	Init->PageDirectory = MmVirtualGetCurrentDirectory(Cpu);

	/* Memset the buffer */
	memset(Init->FpuBuffer, 0, 0x1000);

	/* Done */
	return Init;
}

/* Wake's up CPU */
void _ThreadWakeUpCpu(Cpu_t Cpu)
{
	/* Are we on this cpu? */
	if (Cpu == ApicGetCpu())
	{
		/* Reset Timer Tick */
		ApicWriteLocal(APIC_INITIAL_COUNT, GlbTimerQuantum * 10);

		/* Re-enable timer in one-shot mode */
		ApicWriteLocal(APIC_TIMER_VECTOR, INTERRUPT_TIMER);		//0x20000 - Periodic
	}
	else
		ApicSendIpi((uint8_t)Cpu, INTERRUPT_YIELD);
}

/* Yield current thread */
void _ThreadYield(void)
{
	/* Call the extern */
	_yield();
}

/* Create a new thread */
x86Thread_t *_ThreadInit(Addr_t EntryPoint)
{
	x86Thread_t *t;
	Cpu_t Cpu;

	/* Get cpu */
	Cpu = ApicGetCpu();

	/* Allocate a new thread structure */
	t = (x86Thread_t*)kmalloc(sizeof(x86Thread_t));

	/* Setup */
	t->Context = ContextCreate((Addr_t)EntryPoint);
	t->UserContext = NULL;
	t->Flags = 0;
	
	/* Memory */
	t->Cr3 = memory_get_cr3(); 
	t->PageDirectory = MmVirtualGetCurrentDirectory(Cpu);

	/* FPU */
	t->FpuBuffer = (Addr_t*)kmalloc_a(0x1000);

	/* Memset the buffer */
	memset(t->FpuBuffer, 0, 0x1000);

	/* Done */
	return t;
}

/* Task Switch occurs here */
Registers_t *_ThreadingSwitch(Registers_t *Regs, int PreEmptive, uint32_t *TimeSlice, 
							 uint32_t *TaskPriority)
{
	/* We'll need these */
	Cpu_t Cpu;
	MCoreThread_t *mThread;
	x86Thread_t *tx86;

	/* Sanity */
	if (ThreadingIsEnabled() != 1)
		return Regs;

	/* Get CPU */
	Cpu = ApicGetCpu();

	/* Get thread */
	mThread = ThreadingGetCurrentThread(Cpu);

	/* What the fuck?? */
	assert(mThread != NULL && Regs != NULL);

	/* Cast */
	tx86 = (x86Thread_t*)mThread->ThreadData;

	/* Save FPU/MMX/SSE State */
	if (tx86->Flags & X86_THREAD_USEDFPU)
		save_fpu(tx86->FpuBuffer);

	/* Save stack */
	if (mThread->Flags & THREADING_USERMODE)
		tx86->UserContext = Regs;
	else
		tx86->Context = Regs;

	/* Switch */
	mThread = ThreadingSwitch(Cpu, mThread, (uint8_t)PreEmptive);
	tx86 = (x86Thread_t*)mThread->ThreadData;

	/* Update user variables */
	*TimeSlice = mThread->TimeSlice;
	*TaskPriority = mThread->Priority;

	/* Update Addressing Space */
	MmVirtualSwitchPageDirectory(Cpu, (PageDirectory_t*)tx86->PageDirectory, tx86->Cr3);

	/* Set TSS */
	TssUpdateStack(Cpu, (Addr_t)tx86->Context);

	/* Finish Transition */
	if (mThread->Flags & THREADING_TRANSITION)
	{
		mThread->Flags &= ~THREADING_TRANSITION;
		mThread->Flags |= THREADING_USERMODE;
	}

	/* Clear FPU/MMX/SSE */
	tx86->Flags &= ~X86_THREAD_USEDFPU;

	/* Set TS bit in CR0 */
	set_ts();

	/* Return new stack */
	if (mThread->Flags & THREADING_USERMODE)
		return tx86->UserContext;
	else
		return tx86->Context;
}