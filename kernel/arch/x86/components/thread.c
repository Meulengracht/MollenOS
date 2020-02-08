/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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

#include <component/cpu.h>
#include <arch/interrupts.h>
#include <arch/thread.h>
#include <arch/utils.h>
#include <threading.h>
#include <interrupts.h>
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
extern void load_fpu(uintptr_t *buffer);
extern void load_fpu_extended(uintptr_t *buffer);
extern void save_fpu(uintptr_t *buffer);
extern void save_fpu_extended(uintptr_t *buffer);
extern void set_ts(void);
extern void clear_ts(void);
extern void _yield(void);
extern void ContextEnter(Context_t*);

OsStatus_t
ThreadingRegister(
    _In_ MCoreThread_t* Thread)
{
    Thread->Data[THREAD_DATA_FLAGS]      = 0;
    Thread->Data[THREAD_DATA_MATHBUFFER] = (uintptr_t)kmalloc(0x1000);
    if (!Thread->Data[THREAD_DATA_MATHBUFFER]) {
        return OsOutOfMemory;
    }
    
    memset((void*)Thread->Data[THREAD_DATA_MATHBUFFER], 0, 0x1000);
    return OsSuccess;
}

OsStatus_t
ThreadingUnregister(
    _In_ MCoreThread_t* Thread)
{
    assert(Thread != NULL);
    assert(Thread->Data[THREAD_DATA_MATHBUFFER] != 0);
    
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
    // instead keep track of how nested we are, flag for yield and do it on the way
    // out of last nesting to ensure we've run all interrupt handlers
    if (InterruptGetActiveStatus()) {
        if (ThreadingIsCurrentTaskIdle(ArchGetProcessorCoreId())) {
            OsStatus_t Status = ApicSendInterrupt(InterruptSelf, UUID_INVALID, INTERRUPT_LAPIC);
            if (Status != OsSuccess) {
                FATAL(FATAL_SCOPE_KERNEL, "Failed to deliver IPI signal");
            }
        }
    }
    else {
        _yield();
    }
}

void
SaveThreadState(
    _In_ MCoreThread_t* Thread)
{
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
}

void
RestoreThreadState(
    _In_ MCoreThread_t* Thread)
{
    SystemCpuCore_t* Core = GetCurrentProcessorCore();
    
    // Clear the FPU used flag
    Thread->Data[THREAD_DATA_FLAGS] &= ~X86_THREAD_USEDFPU;
    
    // Load thread-specific resources
    SwitchMemorySpace(Thread->MemorySpace);
    
    TssUpdateIo(Core->Id, (uint8_t*)Thread->MemorySpace->Data[MEMORY_SPACE_IOMAP]);
    TssUpdateThreadStack(Core->Id, (uintptr_t)Thread->Contexts[THREADING_CONTEXT_LEVEL0]);
    set_ts(); // Set task switch bit so we get faults on fpu instructions
    
    // If we are idle task - disable task priority
    if (Thread->Flags & THREADING_IDLE) {
        Core->InterruptPriority = 0;
    }
    else {
        Core->InterruptPriority = 61 - SchedulerObjectGetQueue(Thread->SchedulerObject);
    }
}

void
UpdateThreadContext(
    _In_ MCoreThread_t* Thread,
    _In_ int            ContextType,
    _In_ int            Load)
{
    UUId_t Affinity = SchedulerObjectGetAffinity(Thread->SchedulerObject);
    
    // If we switch into signal stack then make sure we don't overwrite the original
    // interrupt stack for the thread. Otherwise restore the original interrupt stack.
    if (ContextType == THREADING_CONTEXT_SIGNAL) {
        TssUpdateThreadStack(Affinity, (uintptr_t)Thread->Signaling.OriginalContext);
    }
    else {
        TssUpdateThreadStack(Affinity, (uintptr_t)Thread->Contexts[THREADING_CONTEXT_LEVEL0]);
    }
    
    if (Load) {
        ContextEnter(Thread->ContextActive);
    }
}
