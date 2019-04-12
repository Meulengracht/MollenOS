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
 * X86 Common Threading Interface
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

extern size_t GlbTimerQuantum;
extern void init_fpu(void);
extern void load_fpu(uintptr_t *buffer);
extern void load_fpu_extended(uintptr_t *buffer);
extern void save_fpu(uintptr_t *buffer);
extern void save_fpu_extended(uintptr_t *buffer);
extern void set_ts(void);
extern void clear_ts(void);
extern void _yield(void);
extern void enter_thread(Context_t *Regs);

InterruptStatus_t
ThreadingHaltHandler(
    _In_ FastInterruptResources_t*  NotUsed,
    _In_ void*                      Context)
{
    _CRT_UNUSED(NotUsed);
    _CRT_UNUSED(Context);
    ArchProcessorHalt();
    return InterruptHandled;
}

InterruptStatus_t
ThreadingYieldHandler(
    _In_ FastInterruptResources_t*  NotUsed,
    _In_ void*                      Context)
{
    Context_t* NextContext;
    size_t     TimeSlice;
    int        TaskPriority;
    _CRT_UNUSED(NotUsed);

    // Yield => start by sending eoi. It is never certain that we actually return
    // to this function due to how signals are working
    ApicSendEoi(APIC_NO_GSI, INTERRUPT_YIELD);
    NextContext = _GetNextRunnableThread((Context_t*)Context, 0, &TimeSlice, &TaskPriority);

    // If we are idle task - disable timer untill we get woken up
    if (!ThreadingIsCurrentTaskIdle(ArchGetProcessorCoreId())) {
        ApicSetTaskPriority(61 - TaskPriority);
        ApicWriteLocal(APIC_INITIAL_COUNT, GlbTimerQuantum * TimeSlice);
    }
    else {
        ApicSetTaskPriority(0);
        ApicWriteLocal(APIC_INITIAL_COUNT, 0);
    }

    // Manually update interrupt status
    InterruptSetActiveStatus(0);
    enter_thread(NextContext);
    return InterruptHandled;
}

OsStatus_t
ThreadingRegister(
    _In_ MCoreThread_t* Thread)
{
    Thread->Data[THREAD_DATA_FLAGS]      = 0;
    Thread->Data[THREAD_DATA_MATHBUFFER] = (uintptr_t)kmalloc(0x1000);
    memset((void*)Thread->Data[THREAD_DATA_MATHBUFFER], 0, 0x1000);
    return OsSuccess;
}

OsStatus_t
ThreadingUnregister(
    _In_ MCoreThread_t* Thread)
{
    kfree((void*)Thread->Data[THREAD_DATA_MATHBUFFER]);
    return OsSuccess;
}

OsStatus_t
ThreadingFpuException(
    _In_ MCoreThread_t* Thread)
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

void
ThreadingYield(void)
{
    _yield();
}

Context_t*
_GetNextRunnableThread(
    _In_  Context_t* Context,
    _In_  int        PreEmptive,
    _Out_ size_t*    TimeSlice,
    _Out_ int*       TaskQueue)
{
    UUId_t         Cpu    = ArchGetProcessorCoreId();
    MCoreThread_t* Thread = GetCurrentThreadForCore(Cpu);

    // Sanitize the status of threading - return default values
    if (Thread == NULL) {
        *TimeSlice = 20;
        *TaskQueue = 0;
        return Context;
    }
    
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
    Thread     = GetNextRunnableThread(Thread, PreEmptive, &Context);
    *TimeSlice = Thread->TimeSlice;
    *TaskQueue = Thread->Queue;
    Thread->Data[THREAD_DATA_FLAGS] &= ~X86_THREAD_USEDFPU; // Clear the FPU used flag

    // Load thread-specific resources
    SwitchMemorySpace(Thread->MemorySpace);
    TssUpdateThreadStack(Cpu, (uintptr_t)Thread->Contexts[THREADING_CONTEXT_LEVEL0]);
    TssUpdateIo(Cpu, (uint8_t*)Thread->MemorySpace->Data[MEMORY_SPACE_IOMAP]);
    set_ts(); // Set task switch bit so we get faults on fpu instructions
    return Context;
}
