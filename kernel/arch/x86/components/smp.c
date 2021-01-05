/**
 * MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Symmetrical Multiprocessoring
 *  - Contains the implementation of booting and initializing the other
 *    cpu cores in the system if any is present
 */
#define __MODULE "SMP0"
#define __TRACE

#include <component/cpu.h>
#include <arch/interrupts.h>
#include <arch/utils.h>
#include <arch/time.h>
#include <memoryspace.h>
#include <machine.h>
#include <timers.h>
#include <debug.h>
#include <apic.h>
#include <heap.h>
#include <gdt.h>
#include <idt.h>
#include <cpu.h>
#include <string.h>

extern const int   __GlbTramplineCode_length;
extern const char  __GlbTramplineCode[];
static int         JumpSpaceInitialized = 0;

void
PollTimerForMilliseconds(size_t Milliseconds)
{
    volatile clock_t Current;
    clock_t End;
    
    if (TimersGetSystemTick((clock_t*)&Current) != OsSuccess) {
        ArchStallProcessorCore(Milliseconds);
        return;
    }

    End = Current + Milliseconds;
    while (Current < End) {
        TimersGetSystemTick((clock_t*)&Current);
    }
}

void
SmpApplicationCoreEntry(void)
{
    // Disable interrupts and setup descriptors
    InterruptDisable();
    CpuInitializeFeatures();
    GdtInstall();
    IdtInstall();

    // Switch into NUMA memory space if any, otherwise nothing happens
    SwitchMemorySpace(GetCurrentMemorySpace());
    InitializeLocalApicForApplicationCore();

    // Install the TSS before any multitasking
    TssInitialize(0);

    // Register with system - no returning
    ActivateApplicationCore(CpuCoreCurrent());
}

static void
InitializeApplicationJumpSpace(void)
{
    // Intentional use of only 32 bit pointers here as everything will reside
    // in 32 bit space currently. TODO make this also 64 bit.
    uint32_t* CodePointer   = (uint32_t*)((uint8_t*)(&__GlbTramplineCode[0]) + __GlbTramplineCode_length); 
    uint32_t  EntryCode     = LODWORD(((uint32_t*)SmpApplicationCoreEntry));
    int       NumberOfCores = atomic_load(&GetMachine()->NumberOfCores);
    void*     StackSpace    = kmalloc((NumberOfCores - 1) * 0x1000);
    TRACE("InitializeApplicationJumpSpace => allocated %u stacks", (NumberOfCores - 1));

    *(CodePointer - 1) = EntryCode;
    *(CodePointer - 2) = GetCurrentMemorySpace()->Data[MEMORY_SPACE_CR3];
    *(CodePointer - 3) = LODWORD(StackSpace) + 0x1000;
    *(CodePointer - 4) = 0x1000;
    memcpy((void*)MEMORY_LOCATION_TRAMPOLINE_CODE, (char*)__GlbTramplineCode, __GlbTramplineCode_length);
}

void
StartApplicationCore(
    _In_ SystemCpuCore_t* Core)
{
    UUId_t CoreId = CpuCoreId(Core);
    int    Timeout;

    // Initialize jump space
    if (!JumpSpaceInitialized) {
        JumpSpaceInitialized = 1;
        InitializeApplicationJumpSpace();
    }

    // Perform the IPI
    TRACE(" > booting core %" PRIuIN "", CoreId);
    if (ApicPerformIPI(CoreId, 1) != OsSuccess) {
        ERROR(" > failed to boot core %" PRIuIN " (ipi failed)", CoreId);
        return;
    }
    PollTimerForMilliseconds(10);
    // ApicPerformIPI(Core->Id, 0); is needed on older cpus, but not supported on newer.
    // If there is an external DX the AP's will execute code in bios and won't support SIPI

    // Perform the SIPI - some cpu's require two SIPI's.
    if (ApicPerformSIPI(CoreId, MEMORY_LOCATION_TRAMPOLINE_CODE) != OsSuccess) {
        ERROR(" > failed to boot core %" PRIuIN " (sipi failed)", CoreId);
        return;
    }

    // Wait - check if it booted, give it 10ms
    // If it didn't boot then send another SIPI and give up
    Timeout = 10;
    while (!(CpuCoreState(Core) & CpuStateRunning) && Timeout) {
        PollTimerForMilliseconds(1);
        Timeout--;
    }
    
    if (!(CpuCoreState(Core) & CpuStateRunning)) {
        if (ApicPerformSIPI(CoreId, MEMORY_LOCATION_TRAMPOLINE_CODE) != OsSuccess) {
            ERROR(" > failed to boot core %" PRIuIN " (sipi failed)", CoreId);
            return;
        }
    }
}
