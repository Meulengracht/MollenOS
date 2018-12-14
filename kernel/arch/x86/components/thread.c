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

#include <system/thread.h>
#include <system/utils.h>
#include <threading.h>
#include <interrupts.h>
#include <thread.h>
#include <memory.h>
#include <handle.h>
#include <debug.h>
#include <heap.h>
#include <apic.h>
#include <gdt.h>
#include <cpu.h>

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

/* ThreadingHaltHandler
 * Software interrupt handler for the halt command. cli/hlt combo */
InterruptStatus_t
ThreadingHaltHandler(
    _In_ FastInterruptResources_t*  NotUsed,
    _In_ void*                      Context)
{
    _CRT_UNUSED(NotUsed);
    _CRT_UNUSED(Context);
    CpuHalt();
    return InterruptHandled;
}

/* ThreadingYieldHandler
 * Software interrupt handler for the yield command. Emulates a regular timer interrupt. */
InterruptStatus_t
ThreadingYieldHandler(
    _In_ FastInterruptResources_t*  NotUsed,
    _In_ void*                      Context)
{
    Context_t *Regs;
    size_t TimeSlice;
    int TaskPriority;
    _CRT_UNUSED(NotUsed);

    // Yield => start by sending eoi. It is never certain that we actually return
    // to this function due to how signals are working
    ApicSendEoi(APIC_NO_GSI, INTERRUPT_YIELD);
    Regs = _GetNextRunnableThread((Context_t*)Context, 0, &TimeSlice, &TaskPriority);

    // If we are idle task - disable timer untill we get woken up
    if (!ThreadingIsCurrentTaskIdle(CpuGetCurrentId())) {
        ApicSetTaskPriority(61 - TaskPriority);
        ApicWriteLocal(APIC_INITIAL_COUNT, GlbTimerQuantum * TimeSlice);
    }
    else {
        ApicSetTaskPriority(0);
        ApicWriteLocal(APIC_INITIAL_COUNT, 0);
    }

    // Manually update interrupt status
    InterruptSetActiveStatus(0);
    
    // Enter new thread, no returning
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
    // Allocate a new thread context (x86) and zero it out
    Thread->Data[THREAD_DATA_FLAGS]         = 0;
    Thread->Data[THREAD_DATA_MATHBUFFER]    = (uintptr_t)kmalloc_a(0x1000);
    memset((void*)Thread->Data[THREAD_DATA_MATHBUFFER], 0, 0x1000);
    return OsSuccess;
}

/* ThreadingUnregister
 * Unregisters the thread from the system and cleans up any 
 * resources allocated by ThreadingRegister */
OsStatus_t
ThreadingUnregister(
    _In_ MCoreThread_t *Thread)
{
    kfree((void*)Thread->Data[THREAD_DATA_MATHBUFFER]);
    return OsSuccess;
}

/* ThreadingFpuException
 * Handles the fpu exception that might get triggered
 * when performing any float/double instructions. */
OsStatus_t
ThreadingFpuException(
    _In_ MCoreThread_t *Thread)
{
    // Clear the task-switch bit
    clear_ts();

    if (!(Thread->Data[THREAD_DATA_FLAGS] & X86_THREAD_USEDFPU)) {
        if (CpuHasFeatures(CPUID_FEAT_ECX_XSAVE | CPUID_FEAT_ECX_OSXSAVE, 0) == OsSuccess) {
            load_fpu_extended((uintptr_t*)Thread->Data[THREAD_DATA_MATHBUFFER]);
        }
        else {
            load_fpu((uintptr_t*)Thread->Data[THREAD_DATA_MATHBUFFER]);
        }
        Thread->Data[THREAD_DATA_FLAGS] |= X86_THREAD_USEDFPU;
        return OsSuccess;
    }
    return OsError;
}

/* ThreadingYield
 * Yields the current thread control to the scheduler */
void
ThreadingYield(void)
{
    _yield();
}

/* ThreadingSignalDispatch
 * Dispatches a signal to the given thread. This function
 * does not return. */
OsStatus_t
ThreadingSignalDispatch(
    _In_ MCoreThread_t* Thread)
{
    // Now we can enter the signal context 
    // handler, we cannot return from this function
    Thread->Contexts[THREADING_CONTEXT_SIGNAL1] = ContextCreate(Thread->Flags,
        THREADING_CONTEXT_SIGNAL1, Thread->MemorySpace->SignalHandler,
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
    MCoreThread_t* Current;
    UUId_t         Cpu;
    
    Cpu     = CpuGetCurrentId();
    Current = GetCurrentThreadForCore(Cpu);
    
    // If we impersonate ourself, leave
    if (Current == Thread) {
        Current->Flags &= ~(THREADING_IMPERSONATION);
    }
    else {
        Current->Flags |= THREADING_IMPERSONATION;
    }
    TssUpdateIo(Cpu, (uint8_t*)Thread->MemorySpace->Data[MEMORY_SPACE_IOMAP]);
    SwitchSystemMemorySpace(Thread->MemorySpace);
}

/* _GetNextRunnableThread
 * This function loads a new task from the scheduler, it
 * implements the task-switching functionality, which MCore leaves
 * up to the underlying architecture */
Context_t*
_GetNextRunnableThread(
    _In_ Context_t  *Context,
    _In_ int         PreEmptive,
    _Out_ size_t    *TimeSlice,
    _Out_ int       *TaskQueue)
{
    // Variables
    UUId_t Cpu              = CpuGetCurrentId();
    MCoreThread_t *Thread   = GetCurrentThreadForCore(Cpu);

    // Sanitize the status of threading - return default values
    if (Thread == NULL) {
        *TimeSlice      = 20;
        *TaskQueue      = 0;
        return Context;
    }
    assert(!(Thread->Flags & THREADING_IMPERSONATION));
    
    // Save FPU/MMX/SSE information if it's
    // been used, otherwise skip this and save time
    if (Thread->Data[THREAD_DATA_FLAGS] & X86_THREAD_USEDFPU) {
        if (CpuHasFeatures(CPUID_FEAT_ECX_XSAVE | CPUID_FEAT_ECX_OSXSAVE, 0) == OsSuccess) {
            save_fpu_extended((uintptr_t*)Thread->Data[THREAD_DATA_MATHBUFFER]);
        }
        else {
            save_fpu((uintptr_t*)Thread->Data[THREAD_DATA_MATHBUFFER]);
        }
    }

    // Get a new thread for us to enter
    Thread      = GetNextRunnableThread(Thread, PreEmptive, &Context);
    *TimeSlice  = Thread->TimeSlice;
    *TaskQueue  = Thread->Queue;
    Thread->Data[THREAD_DATA_FLAGS] &= ~X86_THREAD_USEDFPU; // Clear the FPU used flag

    // Load thread-specific resources
    SwitchSystemMemorySpace(Thread->MemorySpace);
    TssUpdateThreadStack(Cpu, (uintptr_t)Thread->Contexts[THREADING_CONTEXT_LEVEL0]);
    TssUpdateIo(Cpu, (uint8_t*)Thread->MemorySpace->Data[MEMORY_SPACE_IOMAP]);
    set_ts(); // Set task switch bit so we get faults on fpu instructions

    // Handle any signals pending for thread
    SignalProcess(Thread->Id);
    return Context;
}
