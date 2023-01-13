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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Symmetrical Multiprocessoring
 *  - Contains the implementation of booting and initializing the other
 *    cpu cores in the system if any is present
 */
#define __MODULE "SMP0"
#define __TRACE

#include <assert.h>
#include <arch/interrupts.h>
#include <arch/x86/apic.h>
#include <arch/x86/cpu.h>
#include <component/cpu.h>
#include <component/timer.h>
#include <debug.h>
#include <heap.h>
#include <memoryspace.h>
#include <machine.h>
#include <string.h>

#include "../../../components/cpu_private.h"


#if defined(__i386__)
#include <arch/x86/x32/gdt.h>
#include <arch/x86/x32/idt.h>
#else
#include <arch/x86/x64/gdt.h>
#include <arch/x86/x64/idt.h>
#endif

extern const int   __GlbTramplineCode_length;
extern const char __GlbTramplineCode[];
static int        g_jumpSpaceInitialized = 0;

void
SmpApplicationCoreEntry(void)
{
    // Clear out interrupts untill the core is up and running. At this point we are
    // running in BSP memory space, with BSP's TLS data.
    InterruptDisable();
    GdtInstall();
    IdtInstall();
    CpuInitializeFeatures();

    // Initialize the local apic, so we are ready to receive interrupts, and invoke systems
    // that rely on being able to retrieve the core id. Again we can do this as we are running
    // inside the BSPs memory space where the Local Apic address is correctly mapped.
    ApicInitializeForApplicationCore();

    // We are now loaded up enough to run the shared setup. The shared setup calls functions
    // in this order:
    // Creates new memory space
    // Initializes threading
    // Enables interrupts
    CpuCoreStart();
}

static void
InitializeApplicationJumpSpace(void)
{
    uint64_t* codePointer = (uint64_t*)((uint8_t*)(&__GlbTramplineCode[0]) + __GlbTramplineCode_length);
    uint64_t  entryCode     = (uint64_t)SmpApplicationCoreEntry;
    int       numberOfCores = atomic_load(&GetMachine()->NumberOfCores);
    void*     stackSpace    = kmalloc((numberOfCores - 1) * THREADING_KERNEL_STACK_SIZE);

    TRACE("InitializeApplicationJumpSpace => allocated %u stacks", (numberOfCores - 1));
    assert(stackSpace != NULL);

    *(codePointer - 1) = entryCode;
    *(codePointer - 2) = (uint64_t)GetCurrentMemorySpace()->PlatformData.Cr3PhysicalAddress;
    *(codePointer - 3) = (uint64_t)stackSpace + 0x1000;
    *(codePointer - 4) = 0x1000ULL;
    memcpy((void*)MEMORY_LOCATION_TRAMPOLINE_CODE, (char*)__GlbTramplineCode, __GlbTramplineCode_length);
}

void
StartApplicationCore(
    _In_ SystemCpuCore_t* core)
{
    OSTimestamp_t deadline;
    uuid_t        coreId = CpuCoreId(core);
    int           timeout;

    // Initialize jump space
    if (!g_jumpSpaceInitialized) {
        g_jumpSpaceInitialized = 1;
        InitializeApplicationJumpSpace();
    }

    // Perform the IPI
    TRACE(" > booting core %" PRIuIN "", coreId);
    if (ApicPerformIPI(coreId, 1) != OS_EOK) {
        ERROR(" > failed to boot core %" PRIuIN " (ipi failed)", coreId);
        return;
    }

    SystemTimerGetWallClockTime(&deadline);
    OSTimestampAddNsec(&deadline, &deadline, 10 * NSEC_PER_MSEC);
    SystemTimerStall(&deadline);
    // ApicPerformIPI(Core->Id, 0); is needed on older cpus, but not supported on newer.
    // If there is an external DX the AP's will execute code in bios and won't support SIPI

    // Perform the SIPI - some cpu's require two SIPI's.
    if (ApicPerformSIPI(coreId, MEMORY_LOCATION_TRAMPOLINE_CODE) != OS_EOK) {
        ERROR(" > failed to boot core %" PRIuIN " (sipi failed)", coreId);
        return;
    }

    // Wait - check if it booted, give it 10ms
    // If it didn't boot then send another SIPI and give up
    timeout = 10;
    SystemTimerGetWallClockTime(&deadline);
    while (!(CpuCoreState(core) & CpuStateRunning) && timeout) {
        OSTimestampAddNsec(&deadline, &deadline, NSEC_PER_MSEC);
        SystemTimerStall(&deadline);
        timeout--;
    }
    
    if (!(CpuCoreState(core) & CpuStateRunning)) {
        if (ApicPerformSIPI(coreId, MEMORY_LOCATION_TRAMPOLINE_CODE) != OS_EOK) {
            ERROR(" > failed to boot core %" PRIuIN " (sipi failed)", coreId);
            return;
        }
    }
}
