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
#include <assert.h>

extern const int   __GlbTramplineCode_length;
extern const char  __GlbTramplineCode[];
static int         JumpSpaceInitialized = 0;

void
PollTimerForMilliseconds(size_t Milliseconds)
{
    volatile clock_t current;
    clock_t          end;
    
    if (TimersGetSystemTick((clock_t*)&current) != OsSuccess) {
        ArchStallProcessorCore(Milliseconds);
        return;
    }

    end = current + Milliseconds;
    while (current < end) {
        TimersGetSystemTick((clock_t*)&current);
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

static void InitializeApplicationJumpSpace(void)
{
    // Intentional use of only 32 bit pointers here as everything will reside
    // in 32 bit space currently. TODO make this also 64 bit.
    uint32_t* codePointer = (uint32_t*)((uint8_t*)(&__GlbTramplineCode[0]) + __GlbTramplineCode_length);
    uint32_t entryCode     = LODWORD(((uint32_t*)SmpApplicationCoreEntry));
    int      numberOfCores = atomic_load(&GetMachine()->NumberOfCores);
    void*    stackSpace    = kmalloc((numberOfCores - 1) * 0x1000);

    TRACE("InitializeApplicationJumpSpace => allocated %u stacks", (numberOfCores - 1));
    assert(stackSpace != NULL);

    *(codePointer - 1) = entryCode;
    *(codePointer - 2) = GetCurrentMemorySpace()->Data[MEMORY_SPACE_CR3];
    *(codePointer - 3) = LODWORD(stackSpace) + 0x1000;
    *(codePointer - 4) = 0x1000;
    memcpy((void*)MEMORY_LOCATION_TRAMPOLINE_CODE, (char*)__GlbTramplineCode, __GlbTramplineCode_length);
}

void
StartApplicationCore(
    _In_ SystemCpuCore_t* Core)
{
    UUId_t coreId = CpuCoreId(Core);
    int    timeout;

    // Initialize jump space
    if (!JumpSpaceInitialized) {
        JumpSpaceInitialized = 1;
        InitializeApplicationJumpSpace();
    }

    // Perform the IPI
    TRACE(" > booting core %" PRIuIN "", coreId);
    if (ApicPerformIPI(coreId, 1) != OsSuccess) {
        ERROR(" > failed to boot core %" PRIuIN " (ipi failed)", coreId);
        return;
    }
    PollTimerForMilliseconds(10);
    // ApicPerformIPI(Core->Id, 0); is needed on older cpus, but not supported on newer.
    // If there is an external DX the AP's will execute code in bios and won't support SIPI

    // Perform the SIPI - some cpu's require two SIPI's.
    if (ApicPerformSIPI(coreId, MEMORY_LOCATION_TRAMPOLINE_CODE) != OsSuccess) {
        ERROR(" > failed to boot core %" PRIuIN " (sipi failed)", coreId);
        return;
    }

    // Wait - check if it booted, give it 10ms
    // If it didn't boot then send another SIPI and give up
    timeout = 10;
    while (!(CpuCoreState(Core) & CpuStateRunning) && timeout) {
        PollTimerForMilliseconds(1);
        timeout--;
    }
    
    if (!(CpuCoreState(Core) & CpuStateRunning)) {
        if (ApicPerformSIPI(coreId, MEMORY_LOCATION_TRAMPOLINE_CODE) != OsSuccess) {
            ERROR(" > failed to boot core %" PRIuIN " (sipi failed)", coreId);
            return;
        }
    }
}
