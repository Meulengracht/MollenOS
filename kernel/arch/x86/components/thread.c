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
 * MollenOS x86 Common Threading Interface
 * - Contains shared x86 threading routines
 */

/* Includes 
 * - System */
#include "../../arch.h"
#include <threading.h>
#include <process/phoenix.h>
#include <thread.h>
#include <memory.h>
#include <heap.h>
#include <apic.h>
#include <gdt.h>
#include <log.h>

/* Includes
 * - Library */
#include <assert.h>
#include <string.h>
#include <stdio.h>

/* Extern access, we need access
 * to the timer-quantum and a bunch of 
 * assembly functions */
__CRT_EXTERN size_t GlbTimerQuantum;
__CRT_EXTERN void save_fpu(Addr_t *buffer);
__CRT_EXTERN void set_ts(void);
__CRT_EXTERN void _yield(void);
__CRT_EXTERN void enter_thread(Registers_t *Regs);
__CRT_EXTERN void enter_signal(Registers_t *Regs, Addr_t Handler, int Signal, Addr_t Return);
__CRT_EXTERN void RegisterDump(Registers_t *Regs);

/* Globals,
 * Keep track of whether or not init code has run */
int __GlbThreadX86Initialized = 0;

/* The yield interrupt code for switching tasks
 * and is controlled by software interrupts, the yield interrupt
 * also like the apic switch need to reload the apic timer as it
 * controlls the primary switch */
int ThreadingYield(void *Args)
{
	/* Variables we will need for loading
	 * a new task */
	Registers_t *Regs = NULL;
	Cpu_t CurrCpu = ApicGetCpu();

	/* These will be assigned from the 
	 * _switch function, but set them in
	 * case threading is not initialized yet */
	size_t TimeSlice = 20;
	int TaskPriority = 0;

	/* Before we do anything, send EOI so 
	 * we don't forget :-) */
	ApicSendEoi(APIC_NO_GSI, INTERRUPT_YIELD);

	/* Switch Task, if there is no threading enabled yet
	 * it should return the same structure as we give */
	Regs = _ThreadingSwitch((Registers_t*)Args, 0, &TimeSlice, &TaskPriority);

	/* If we just got hold of idle task, well fuck it disable timer 
	 * untill we get another task */
	if (!ThreadingIsCurrentTaskIdle(CurrCpu)) {
		ApicSetTaskPriority(61 - TaskPriority);
		ApicWriteLocal(APIC_INITIAL_COUNT, GlbTimerQuantum * TimeSlice);
	}
	else {
		ApicSetTaskPriority(0);
		ApicWriteLocal(APIC_INITIAL_COUNT, 0);
	}

	/* Enter new thread */
	enter_thread(Regs);

	/* Never reached */
	return X86_IRQ_HANDLED;
}

/* IThreadCreate
 * Initializes a new x86-specific thread context
 * for the given threading flags, also initializes
 * the yield interrupt handler first time its called */
void *IThreadCreate(Flags_t ThreadFlags, Addr_t EntryPoint)
{
	/* Variables for initialization */
	x86Thread_t *Thread = NULL;

	/* Allocate a new thread context (x86) 
	 * and zero it out */
	Thread = (x86Thread_t*)kmalloc(sizeof(x86Thread_t));
	memset(Thread, 0, sizeof(x86Thread_t));

	/* Allocate a new buffer for FPU operations  
	 * and zero out the buffer space */
	Thread->FpuBuffer = kmalloc_a(0x1000);
	memset(Thread->FpuBuffer, 0, 0x1000);

	/* Initialize rest of params */
	Thread->Flags = X86_THREAD_FPU_INITIALISED | X86_THREAD_USEDFPU;

	/* Don't create contexts for idle threads 
	 * Otherwise setup a kernel stack */
	if (!(ThreadFlags & THREADING_IDLE)) {
		Thread->Context = ContextCreate(THREADING_KERNELMODE, EntryPoint, NULL);
	}

	/* If its the first time we run, install
	 * the yield interrupt */
	if (__GlbThreadX86Initialized == 0) {
		__GlbThreadX86Initialized = 1;
		InterruptInstallIdtOnly(APIC_NO_GSI, INTERRUPT_YIELD, ThreadingYield, NULL);
	}

	/* Done */
	return Thread;
}

/* IThreadSetupUserMode
 * Initializes user-mode data for the given thread, and
 * allocates all neccessary resources (x86 specific) for
 * usermode operations */
void IThreadSetupUserMode(MCoreThread_t *Thread, Addr_t StackAddress)
{
	/* Initialize a pointer to x86 specific data */
	x86Thread_t *tData = (x86Thread_t*)Thread->ThreadData;
	_CRT_UNUSED(StackAddress);

	/* Initialize a user/driver-context based on
	 * the requested runmode */
	tData->UserContext = ContextCreate(Thread->Flags,
		(Addr_t)Thread->Function, (Addr_t*)Thread->Args);

	/* Disable all port-access */
	memset(&tData->IoMap[0], 0xFF, GDT_IOMAP_SIZE);
}

/* IThreadDestroy
 * Free's all the allocated resources for x86
 * specific threading operations */
void IThreadDestroy(MCoreThread_t *Thread)
{
	/* Initialize a pointer to x86 specific data */
	x86Thread_t *tData = (x86Thread_t*)Thread->ThreadData;

	/* Cleanup both contexts */
	kfree(tData->Context);
	if (tData->UserContext != NULL) {
		kfree(tData->UserContext);
	}

	/* Free fpu buffer and the
	 * base structure */
	kfree(tData->FpuBuffer);
	kfree(tData);
}

/* IThreadWakeCpu
 * Wake's the target cpu from an idle thread
 * by sending it an yield IPI */
void IThreadWakeCpu(Cpu_t Cpu)
{
	/* Send an IPI to the cpu */
	ApicSendIpi(Cpu, INTERRUPT_YIELD);
}

/* IThreadYield
 * Yields the current thread control to the scheduler */
void IThreadYield(void)
{
	_yield();
}

/* Dispatches a signal to the given process 
 * signals will always be dispatched to main thread */
void SignalDispatch(MCoreAsh_t *Ash, MCoreSignal_t *Signal)
{
	/* Variables */
	MCoreThread_t *Thread = ThreadingGetThread(Ash->MainThread);
	x86Thread_t *Thread86 = (x86Thread_t*)Thread->ThreadData;
	Registers_t *Regs = NULL;

	/* User or kernel mode thread? */
	if (Thread->Flags & THREADING_USERMODE)
		Regs = Thread86->UserContext;
	else
		Regs = Thread86->Context;

	/* Store current context */
	memcpy(&Signal->Context, Regs, sizeof(Registers_t));

	/* Now we can enter the signal context 
	 * handler, we cannot return from this function */
	enter_signal(Regs, Signal->Handler, Signal->Signal, MEMORY_LOCATION_SIGNAL_RET);
}

/* This function loads a new task from the scheduler, it
 * implements the task-switching functionality, which MCore leaves
 * up to the underlying architecture */
Registers_t *_ThreadingSwitch(Registers_t *Regs, 
	int PreEmptive, size_t *TimeSlice, int *TaskQueue)
{
	/* Variables we will need for the
	 * context switch */
	MCoreThread_t *Thread = NULL;
	x86Thread_t *Tx = NULL;
	Cpu_t Cpu = 0;

	/* Start out by sanitizing the state
	 * of threading, don't schedule */
	if (ThreadingIsEnabled() != 1) {
		return Regs;
	}

	/* Lookup cpu and threading info */
	Cpu = ApicGetCpu();
	Thread = ThreadingGetCurrentThread(Cpu);

	/* ASsert some sanity in this function! */
	assert(Thread != NULL && Regs != NULL);

	/* Initiate the x86 specific thread data pointer */
	Tx = (x86Thread_t*)Thread->ThreadData;

	/* Save FPU/MMX/SSE information if it's
	 * been used, otherwise skip this and save time */
	if (Tx->Flags & X86_THREAD_USEDFPU) {
		save_fpu(Tx->FpuBuffer);
	}

	/* Save stack, we have a few cases here. 
	 * We are using kernel stack in case of two things:
	 * 1. Transitioning threads
	 * 2. Kernel threads (surprise!) */
	if ((THREADING_RUNMODE(Thread->Flags) == THREADING_KERNELMODE)
		|| (Thread->Flags & THREADING_SWITCHMODE)) {
		Tx->Context = Regs;
	}
	else {
		Tx->UserContext = Regs;
	}
	
	/* Lookup a new thread and initiate our pointers */
	Thread = ThreadingSwitch(Cpu, Thread, PreEmptive);
	Tx = (x86Thread_t*)Thread->ThreadData;

	/* Update scheduler variables */
	*TimeSlice = Thread->TimeSlice;
	*TaskQueue = Thread->Queue;

	/* Update addressing space */
	MmVirtualSwitchPageDirectory(Cpu, 
		(PageDirectory_t*)Thread->AddressSpace->PageDirectory, 
		Thread->AddressSpace->Cr3);

	/* Update TSS information (stack/iomap) */
	TssUpdateStack(Cpu, (Addr_t)Tx->Context);
	TssUpdateIo(Cpu, &Tx->IoMap[0]);

	/* Clear FPU/MMX/SSE flags */
	Tx->Flags &= ~X86_THREAD_USEDFPU;

	/* We want to handle any signals if neccessary
	 * before we handle the transition */
	SignalHandle(Thread->Id);

	/* Handle the transition, we have to remove
	 * the bit as we now have transitioned */
	if (Thread->Flags & THREADING_TRANSITION) {
		Thread->Flags &= ~(THREADING_SWITCHMODE | THREADING_TRANSITION);
	}

	/* Set TS bit in CR0 */
	set_ts();

	/* Return new stack */
	if ((THREADING_RUNMODE(Thread->Flags) == THREADING_KERNELMODE)
		|| (Thread->Flags & THREADING_SWITCHMODE)) {
		return Tx->Context;
	}	
	else {
		return Tx->UserContext;
	}
}
