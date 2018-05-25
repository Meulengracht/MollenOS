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
#define __MODULE "XTIF"

/* Includes 
 * - System */
#include <system/thread.h>
#include <system/utils.h>

#include <threading.h>
#include <process/phoenix.h>
#include <interrupts.h>
#include <thread.h>
#include <memory.h>
#include <debug.h>
#include <heap.h>
#include <apic.h>
#include <gdt.h>
#include <cpu.h>

/* Includes
 * - Library */
#include <assert.h>
#include <string.h>
#include <stdio.h>

/* Externs
 * Extern access, we need access to the timer-quantum 
 * and a bunch of  assembly functions */
__EXTERN size_t GlbTimerQuantum;
__EXTERN void init_fpu(void);
__EXTERN void load_fpu(uintptr_t *buffer);
__EXTERN void load_fpu_extended(uintptr_t *buffer);
__EXTERN void save_fpu(uintptr_t *buffer);
__EXTERN void save_fpu_extended(uintptr_t *buffer);
__EXTERN void set_ts(void);
__EXTERN void clear_ts(void);
__EXTERN void _yield(void);
__EXTERN void enter_thread(Context_t *Regs);

/* Globals,
 * Keep track of whether or not init code has run */
static Collection_t *Threads            = NULL;
static int ThreadsInitialized           = 0;

/* The yield interrupt code for switching tasks
 * and is controlled by software interrupts, the yield interrupt
 * also like the apic switch need to reload the apic timer as it
 * controlls the primary switch */
InterruptStatus_t
ThreadingYieldHandler(
    _In_ void *Context)
{
    // Variables
	Context_t *Regs     = NULL;
	size_t TimeSlice    = 20;
	int TaskPriority    = 0;

    // Yield => start by sending eoi
	ApicSendEoi(APIC_NO_GSI, INTERRUPT_YIELD);

	/* Switch Task, if there is no threading enabled yet
	 * it should return the same structure as we give */
	Regs = _ThreadingSwitch((Context_t*)Context, 0, &TimeSlice, &TaskPriority);

	/* If we just got hold of idle task, well fuck it disable timer 
	 * untill we get another task */
	if (!ThreadingIsCurrentTaskIdle(CpuGetCurrentId())) {
		ApicSetTaskPriority(61 - TaskPriority);
		ApicWriteLocal(APIC_INITIAL_COUNT, GlbTimerQuantum * TimeSlice);
	}
	else {
		ApicSetTaskPriority(0);
		ApicWriteLocal(APIC_INITIAL_COUNT, 0);
	}
	enter_thread(Regs);
	return InterruptHandled;
}

/* ThreadingRegister
 * Initializes a new arch-specific thread context
 * for the given threading flags, also initializes
 * the yield interrupt handler first time its called */
OsStatus_t
ThreadingRegister(
    _In_ MCoreThread_t *Thread)
{
	// Variables
	MCoreInterrupt_t Interrupt;
	x86Thread_t *Thread86       = NULL;
    DataKey_t Key;

	// Allocate a new thread context (x86) 
	// and zero it out
    Key.Value           = (int)Thread->Id;
	Thread86            = (x86Thread_t*)kmalloc(sizeof(x86Thread_t));
	memset(Thread86, 0, sizeof(x86Thread_t));

	// Allocate a new buffer for FPU operations  
	// and zero out the buffer space
	Thread86->FpuBuffer = kmalloc_a(0x1000);
	memset(Thread86->FpuBuffer, 0, 0x1000);

	// Disable all port-access
    if (THREADING_RUNMODE(Thread->Flags) != THREADING_KERNELMODE) {
	    memset(&Thread86->IoMap[0], 0xFF, GDT_IOMAP_SIZE);
    }

	// If its the first time we run, install the yield interrupt
	if (ThreadsInitialized == 0) {
        Threads                     = CollectionCreate(KeyInteger);
        Interrupt.Vectors[0]        = INTERRUPT_YIELD;
        Interrupt.Vectors[1]        = INTERRUPT_NONE;
		Interrupt.Line              = INTERRUPT_NONE;
		Interrupt.Pin               = INTERRUPT_NONE;
        Interrupt.FastHandler       = ThreadingYieldHandler;
		Interrupt.Data              = NULL;
        InterruptRegister(&Interrupt, INTERRUPT_SOFT | INTERRUPT_KERNEL 
            | INTERRUPT_NOTSHARABLE | INTERRUPT_CONTEXT);
		ThreadsInitialized          = 1;
	}
	return CollectionAppend(Threads, CollectionCreateNode(Key, Thread86));
}

/* ThreadingUnregister
 * Unregisters the thread from the system and cleans up any 
 * resources allocated by ThreadingRegister */
OsStatus_t
ThreadingUnregister(
    _In_ MCoreThread_t *Thread)
{
	// Variables
	x86Thread_t *tData = NULL;
    DataKey_t Key;

    // Get data from our list
    Key.Value   = (int)Thread->Id; 
    tData       =  (x86Thread_t*)CollectionGetDataByKey(Threads, Key, 0);
    if (tData == NULL) {
        return OsError;
    }

    // Cleanup
    CollectionRemoveByKey(Threads, Key);
	kfree(tData->FpuBuffer);
	kfree(tData);
    return OsSuccess;
}

/* ThreadingFpuException
 * Handles the fpu exception that might get triggered
 * when performing any float/double instructions. */
OsStatus_t
ThreadingFpuException(
    _In_ MCoreThread_t *Thread)
{
    // Variables
    x86Thread_t *tData = NULL;
    DataKey_t Key;

    // Clear the task-switch bit
    clear_ts();

    // Get data from our list
    Key.Value   = (int)Thread->Id; 
    tData       =  (x86Thread_t*)CollectionGetDataByKey(Threads, Key, 0);
    if (tData == NULL) {
        ERROR("Thread data was not found for thread %u", Thread->Id);
        return OsError;
    }

    if (!(tData->Flags & X86_THREAD_USEDFPU)) {
        if (CpuHasFeatures(CPUID_FEAT_ECX_XSAVE | CPUID_FEAT_ECX_OSXSAVE, 0) == OsSuccess) {
            load_fpu_extended(tData->FpuBuffer);
        }
        else {
            load_fpu(tData->FpuBuffer);
        }
        tData->Flags |= X86_THREAD_USEDFPU;
        return OsSuccess;
    }
    return OsError;
}

/* ThreadingIoSet
 * Set's the io status of the given thread. */
OsStatus_t
ThreadingIoSet(
    _In_ MCoreThread_t *Thread,
    _In_ uint16_t       Port,
    _In_ int            Enable)
{
    // Variables
    x86Thread_t *tData = NULL;
    DataKey_t Key;

    // Get data from our list
    Key.Value   = (int)Thread->Id; 
    tData       =  (x86Thread_t*)CollectionGetDataByKey(Threads, Key, 0);
    if (tData == NULL) {
        return OsError;
    }

    // Update thread's io-map
    if (Enable) {
        tData->IoMap[Port / 8] &= ~(1 << (Port % 8));
    }
    else {
        tData->IoMap[Port / 8] |= (1 << (Port % 8));
    }
    return OsSuccess;
}

/* ThreadingWakeCpu
 * Wake's the target cpu from an idle thread
 * by sending it an yield IPI */
void
ThreadingWakeCpu(
    _In_ UUId_t Cpu) {
	ApicSendInterrupt(InterruptSpecific, Cpu, INTERRUPT_YIELD);
}

/* ThreadingYield
 * Yields the current thread control to the scheduler */
void
ThreadingYield(void) {
	_yield();
}

/* ThreadingSignalDispatch
 * Dispatches a signal to the given thread. This function
 * does not return. */
OsStatus_t
ThreadingSignalDispatch(
	_In_ MCoreThread_t *Thread)
{
	// Variables
    MCoreAsh_t *Process     = PhoenixGetAsh(Thread->AshId);

	// Now we can enter the signal context 
	// handler, we cannot return from this function
    Thread->Contexts[THREADING_CONTEXT_SIGNAL1] = ContextCreate(Thread->Flags,
        THREADING_CONTEXT_SIGNAL1, Process->SignalHandler,
        MEMORY_LOCATION_SIGNAL_RET, Thread->ActiveSignal.Signal, 0);
    TssUpdateThreadStack(CpuGetCurrentId(), (uintptr_t)Thread->Contexts[THREADING_CONTEXT_SIGNAL0]);
    enter_thread(Thread->Contexts[THREADING_CONTEXT_SIGNAL1]);
	return OsSuccess;
}

/* ThreadingImpersonate
 * This function switches the current runtime-context
 * out with the given thread context, this should only
 * be used as a temporary way of impersonating another thread */
void
ThreadingImpersonate(
    _In_ MCoreThread_t *Thread)
{
    // Variables
    MCoreThread_t *Current  = NULL;
	x86Thread_t *SubContext = NULL;
	UUId_t Cpu              = 0;
    DataKey_t Key;

	// Instantiate values
    Key.Value       = (int)Thread->Id;
    Cpu             = CpuGetCurrentId();
    Current         = ThreadingGetCurrentThread(Cpu);
	SubContext      = (x86Thread_t*)CollectionGetDataByKey(Threads, Key, 0);
    
    // If we impersonate ourself, leave
    if (Current == Thread) {
        Current->Flags &= ~(THREADING_IMPERSONATION);
    }
    else {
        Current->Flags |= THREADING_IMPERSONATION;
    }

    // Load resources
	TssUpdateIo(Cpu, &SubContext->IoMap[0]);
	UpdateVirtualAddressingSpace(
		(void*)Thread->AddressSpace->Data[ASPACE_DATA_PDPOINTER],
		Thread->AddressSpace->Data[ASPACE_DATA_CR3]);
}

/* _ThreadingSwitch
 * This function loads a new task from the scheduler, it
 * implements the task-switching functionality, which MCore leaves
 * up to the underlying architecture */
Context_t*
_ThreadingSwitch(
    _In_ Context_t  *Context,
    _In_ int         PreEmptive,
    _Out_ size_t    *TimeSlice,
    _Out_ int       *TaskQueue)
{
	// Variables
	MCoreThread_t *Thread   = NULL;
	x86Thread_t *Threadx    = NULL;
	UUId_t Cpu              = CpuGetCurrentId();
    DataKey_t Key;

    // Sanitize the status of threading - return default values
	if (ThreadingIsEnabled() != 1 || ThreadingGetCurrentThread(Cpu) == NULL) {
        *TimeSlice      = 20;
        *TaskQueue      = 0;
		return Context;
    }

	// Instantiate variables
	Thread      = ThreadingGetCurrentThread(Cpu);
    Key.Value   = (int)Thread->Id;
	Threadx     = (x86Thread_t*)CollectionGetDataByKey(Threads, Key, 0);
    assert(Thread != NULL && Threadx != NULL);
    
    // Sanitize impersonation status, don't schedule
    if (Thread->Flags & THREADING_IMPERSONATION) {
        *TimeSlice      = Thread->TimeSlice;
        *TaskQueue      = Thread->Queue;
        return Context;
    }

	// Save FPU/MMX/SSE information if it's
	// been used, otherwise skip this and save time
	if (Threadx->Flags & X86_THREAD_USEDFPU) {
        if (CpuHasFeatures(CPUID_FEAT_ECX_XSAVE | CPUID_FEAT_ECX_OSXSAVE, 0) == OsSuccess) {
            save_fpu_extended(Threadx->FpuBuffer);
        }
        else {
            save_fpu(Threadx->FpuBuffer);
        }
	}

	// Get a new thread for us to enter
	Thread      = ThreadingSwitch(Thread, PreEmptive, &Context);
    Key.Value   = (int)Thread->Id;
	Threadx     = (x86Thread_t*)CollectionGetDataByKey(Threads, Key, 0);

	// Update out's
	*TimeSlice  = Thread->TimeSlice;
	*TaskQueue  = Thread->Queue;

	// Load thread-specific resources
	UpdateVirtualAddressingSpace( 
		(void*)Thread->AddressSpace->Data[ASPACE_DATA_PDPOINTER], 
		Thread->AddressSpace->Data[ASPACE_DATA_CR3]);
	TssUpdateThreadStack(Cpu, (uintptr_t)Thread->Contexts[THREADING_CONTEXT_LEVEL0]);
	TssUpdateIo(Cpu, &Threadx->IoMap[0]);

    // Clear fpu flags and set task switch
    Threadx->Flags &= ~X86_THREAD_USEDFPU;
	set_ts();

    // Handle any signals pending for thread
	SignalHandle(Thread->Id);
    return Context;
}
