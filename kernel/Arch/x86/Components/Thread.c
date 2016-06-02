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
extern uint32_t GlbTimerQuantum;
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
	Regs = _ThreadingSwitch((Registers_t*)Args, 0, &TimeSlice, &TaskPriority);

	/* If we just got hold of idle task, well fuck it disable timer
	* untill we get another task */
	if (!(ThreadingGetCurrentThread(CurrCpu)->Flags & THREADING_IDLE))
	{
		/* Set Task Priority */
		ApicSetTaskPriority(61 - TaskPriority);

		/* Restart timer */
		ApicWriteLocal(APIC_INITIAL_COUNT, GlbTimerQuantum * TimeSlice);
	}
	else
	{
		/* Set low priority */
		ApicSetTaskPriority(0);

		/* Disable timer */
		ApicWriteLocal(APIC_INITIAL_COUNT, 0);
	}

	/* Enter new thread */
	enter_thread(Regs);

	/* Never reached */
	return X86_IRQ_HANDLED;
}

/* Initialization 
 * Creates the main thread */
void *IThreadInitBoot(void)
{
	/* Vars */
	x86Thread_t *Init;

	/* Setup initial thread */
	Init = (x86Thread_t*)kmalloc(sizeof(x86Thread_t));
	memset(Init, 0, sizeof(x86Thread_t));

	Init->FpuBuffer = kmalloc_a(0x1000);
	Init->Flags = X86_THREAD_FPU_INITIALISED | X86_THREAD_USEDFPU;

	/* Memset the buffer */
	memset(Init->FpuBuffer, 0, 0x1000);

	/* Install Yield */
	InterruptInstallIdtOnly(0xFFFFFFFF, INTERRUPT_YIELD, ThreadingYield, NULL);

	/* Done */
	return Init;
}

/* Initialises AP task */
void *IThreadInitAp(void)
{
	/* Vars */
	x86Thread_t *Init;

	/* Setup initial thread */
	Init = (x86Thread_t*)kmalloc(sizeof(x86Thread_t));
	memset(Init, 0, sizeof(x86Thread_t));

	Init->FpuBuffer = kmalloc_a(0x1000);
	Init->Flags = X86_THREAD_FPU_INITIALISED | X86_THREAD_USEDFPU;

	/* Memset the buffer */
	memset(Init->FpuBuffer, 0, 0x1000);

	/* Done */
	return Init;
}

/* Wake's up CPU */
void IThreadWakeCpu(Cpu_t Cpu)
{
	/* Send an IPI to the cpu */
	ApicSendIpi((uint8_t)Cpu, INTERRUPT_YIELD);
}

/* Yield current thread */
_CRT_EXPORT void IThreadYield(void)
{
	/* Call the extern */
	_yield();
}

/* Create a new thread */
void *IThreadInit(Addr_t EntryPoint)
{
	/* Vars */
	x86Thread_t *t;
	Cpu_t Cpu;

	/* Get cpu */
	Cpu = ApicGetCpu();

	/* Allocate a new thread structure */
	t = (x86Thread_t*)kmalloc(sizeof(x86Thread_t));
	memset(t, 0, sizeof(x86Thread_t));

	/* Setup */
	t->Context = ContextCreate((Addr_t)EntryPoint);

	/* FPU */
	t->FpuBuffer = (Addr_t*)kmalloc_a(0x1000);

	/* Memset the buffer */
	memset(t->FpuBuffer, 0, 0x1000);

	/* Done */
	return t;
}

/* Frees thread resources */
void IThreadDestroy(void *ThreadData)
{
	/* Cast */
	x86Thread_t *Thread = (x86Thread_t*)ThreadData;

	/* Cleanup Contexts */
	kfree(Thread->Context);

	/* Not all has user context */
	if (Thread->UserContext != NULL)
		kfree(Thread->UserContext);

	/* Free fpu buffer */
	kfree(Thread->FpuBuffer);

	/* Free structure */
	kfree(Thread);
}

/* Setup Usermode */
void IThreadInitUserMode(void *ThreadData, Addr_t StackAddr, Addr_t EntryPoint, Addr_t ArgumentAddress)
{
	/* Cast */
	x86Thread_t *t = (x86Thread_t*)ThreadData;

	/* Create user-context */
	t->UserContext = ContextUserCreate(StackAddr, EntryPoint, (Addr_t*)ArgumentAddress);
}

/* Task Switch occurs here */
Registers_t *_ThreadingSwitch(Registers_t *Regs, int PreEmptive, uint32_t *TimeSlice, 
							 uint32_t *TaskQueue)
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
	*TaskQueue = mThread->Queue;

	/* Update Addressing Space */
	MmVirtualSwitchPageDirectory(Cpu, 
		(PageDirectory_t*)mThread->AddrSpace->PageDirectory, mThread->AddrSpace->Cr3);

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