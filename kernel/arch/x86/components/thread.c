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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * X86 Common Threading Interface
 * - Contains shared x86 threading routines
 */
#define __MODULE "XTIF"
//#define __TRACE

#include <component/cpu.h>
#include <arch/thread.h>
#include <arch/utils.h>
#include <arch/x86/arch.h>
#include <arch/x86/apic.h>
#include <arch/x86/cpu.h>
#include <ddk/ddkdefs.h>
#include <threading.h>
#include <interrupts.h>
#include <debug.h>
#include <heap.h>

#include <assert.h>
#include <string.h>

#if defined(__i386__)
#include <arch/x86/x32/gdt.h>
#else
#include <arch/x86/x64/gdt.h>
#endif

#define X86_THREAD_UPDATETLS 0x1
#define X86_THREAD_USEDFPU   0x2

extern void load_fpu(uintptr_t *buffer);
extern void load_fpu_extended(uintptr_t *buffer);
extern void save_fpu(uintptr_t *buffer);
extern void save_fpu_extended(uintptr_t *buffer);
extern void set_ts(void);
extern void clear_ts(void);
extern void _yield(void);

oserr_t
ArchThreadInitialize(
    _In_ Thread_t* thread)
{
    PlatformThreadBlock_t* threadData = ThreadPlatformBlock(thread);
    oserr_t                oserr;

    if (!threadData) {
        return OS_EINVALPARAMS;
    }

    // Are we creating the idle thread? Then perform some additional setup for the core
    if (ThreadFlags(thread) & THREADING_IDLE) {
        SystemCpuCore_t* currentCore = GetProcessorCore(ArchGetProcessorCoreId());
        oserr = TssInitialize(CpuCorePlatformBlock(currentCore));
        if (oserr != OS_EOK) {
            return oserr;
        }
    }

    threadData->Flags      = X86_THREAD_UPDATETLS;
    threadData->MathBuffer = kmalloc(0x1000);
    if (!threadData->MathBuffer) {
        return OS_EOOM;
    }
    memset(threadData->MathBuffer, 0, 0x1000);
    return OS_EOK;
}

oserr_t
ArchThreadDestroy(
    _In_ Thread_t* thread)
{
    PlatformThreadBlock_t* threadData = ThreadPlatformBlock(thread);
    if (!threadData) {
        return OS_EINVALPARAMS;
    }

    if (threadData->MathBuffer) {
        kfree(threadData->MathBuffer);
    }
    return OS_EOK;
}

oserr_t
ThreadingFpuException(
    _In_ Thread_t* thread)
{
    PlatformThreadBlock_t* threadData = ThreadPlatformBlock(thread);
    if (!threadData) {
        return OS_EINVALPARAMS;
    }

    // Clear the task-switch bit
    clear_ts();

    if (!(threadData->Flags & X86_THREAD_USEDFPU)) {
        if (CpuHasFeatures(CPUID_FEAT_ECX_XSAVE | CPUID_FEAT_ECX_OSXSAVE, 0) == OS_EOK) {
            load_fpu_extended((uintptr_t*)threadData->MathBuffer);
        }
        else {
            load_fpu((uintptr_t*)threadData->MathBuffer);
        }
        threadData->Flags |= X86_THREAD_USEDFPU;
        return OS_EOK;
    }
    return OS_EUNKNOWN;
}

void
ArchThreadYield(void)
{
    // Never yield in interrupt handlers, could cause wierd stuff to happen
    // instead keep track of how nested we are, flag for yield and do it on the way
    // out of last nesting to ensure we've run all interrupt handlers
    if (InterruptGetActiveStatus()) {
        if (ThreadIsCurrentIdle(ArchGetProcessorCoreId())) {
            oserr_t osStatus = ApicSendInterrupt(InterruptTarget_SELF, UUID_INVALID, INTERRUPT_LAPIC);
            if (osStatus != OS_EOK) {
                FATAL(FATAL_SCOPE_KERNEL, "Failed to deliver IPI signal");
            }
        }
    }
    else {
        _yield();
    }
}

void
ArchThreadLeave(
    _In_ Thread_t* thread)
{
    PlatformThreadBlock_t* threadData = ThreadPlatformBlock(thread);
    assert(threadData != NULL);

    // Save FPU/MMX/SSE information if it's
    // been used, otherwise skip this and save time
    if (threadData->Flags & X86_THREAD_USEDFPU) {
        if (CpuHasFeatures(CPUID_FEAT_ECX_XSAVE | CPUID_FEAT_ECX_OSXSAVE, 0) == OS_EOK) {
            save_fpu_extended((uintptr_t*)threadData->MathBuffer);
        }
        else {
            save_fpu((uintptr_t*)threadData->MathBuffer);
        }
    }
}

void
ArchThreadEnter(
        _In_ SystemCpuCore_t* cpuCore,
        _In_ Thread_t*        thread)
{
    PlatformCpuCoreBlock_t* coreBlock   = CpuCorePlatformBlock(cpuCore);
    PlatformThreadBlock_t*  threadData  = ThreadPlatformBlock(thread);
    MemorySpace_t*          memorySpace = ThreadMemorySpace(thread);

    // Load thread-specific resources
    MemorySpaceSwitch(memorySpace);

    // Clear the FPU used flag
    threadData->Flags &= ~X86_THREAD_USEDFPU;
    if (threadData->Flags & X86_THREAD_UPDATETLS) {
        threadData->Flags &= ~X86_THREAD_UPDATETLS;
        __set_reserved(0, (uintptr_t)cpuCore);
    }
    
    TssUpdateIo(coreBlock, (uint8_t*)memorySpace->PlatfromData.TssIoMap);
    TssUpdateThreadStack(coreBlock, (uintptr_t)ThreadContext(thread, THREADING_CONTEXT_LEVEL0));
    set_ts(); // Set task switch bit, so we get faults on fpu instructions
}
