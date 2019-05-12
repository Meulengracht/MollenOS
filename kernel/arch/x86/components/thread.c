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
//#define __TRACE

#include <arch/interrupts.h>
#include <arch/thread.h>
#include <arch/utils.h>
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
extern void enter_thread(Context_t *Regs);
extern void load_fpu(uintptr_t *buffer);
extern void load_fpu_extended(uintptr_t *buffer);
extern void save_fpu(uintptr_t *buffer);
extern void save_fpu_extended(uintptr_t *buffer);
extern void set_ts(void);
extern void clear_ts(void);
extern void _yield(void);

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
    // Never yield in interrupt handlers, could cause wierd stuff to happen
    if (InterruptGetActiveStatus()) {
        if (ThreadingIsCurrentTaskIdle(ArchGetProcessorCoreId())) {
            ApicSetTaskPriority(0);
            ApicWriteLocal(APIC_INITIAL_COUNT, GlbTimerQuantum);
        }
    }
    else {
        _yield();
    }
}

void
X86SwitchThread(
    _In_ Context_t* Context,
    _In_ int        Preemptive,
    _In_ size_t     MillisecondsPassed)
{
    UUId_t         CoreId = ArchGetProcessorCoreId();
    MCoreThread_t* Thread = GetCurrentThreadForCore(CoreId);
    MCoreThread_t* NextThread;
    size_t         Deadline;

    // Sanitize the status of threading, if it's not up and running
    // but a timer is, then set default values and return thread
    if (Thread == NULL) {
        ApicSetTaskPriority(0);
        ApicWriteLocal(APIC_INITIAL_COUNT, GlbTimerQuantum * 20);
        InterruptSetActiveStatus(0);
        enter_thread(Context);
        // -- no return
    }
    
    // Store active context
    Thread->ContextActive = Context;
    
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
    NextThread = ThreadingAdvance(Thread, Preemptive, MillisecondsPassed, &Deadline);
    if (NextThread != Thread) {
        // Clear the FPU used flag
        NextThread->Data[THREAD_DATA_FLAGS] &= ~X86_THREAD_USEDFPU;
        
        // Load thread-specific resources
        SwitchMemorySpace(NextThread->MemorySpace);
        TssUpdateThreadStack(CoreId, (uintptr_t)NextThread->Contexts[THREADING_CONTEXT_LEVEL0]);
        TssUpdateIo(CoreId, (uint8_t*)NextThread->MemorySpace->Data[MEMORY_SPACE_IOMAP]);
        set_ts(); // Set task switch bit so we get faults on fpu instructions
    }

    // If we are idle task - disable timer untill we get woken up
    if (NextThread->Flags & THREADING_IDLE) {
        ApicSetTaskPriority(0);
    }
    else {
        ApicSetTaskPriority(61 - NextThread->SchedulerObject->Queue);
        ApicWriteLocal(APIC_INITIAL_COUNT, GlbTimerQuantum * Deadline);
    }
    
    // Manually update interrupt status
    InterruptSetActiveStatus(0);
    enter_thread(NextThread->ContextActive);
}
