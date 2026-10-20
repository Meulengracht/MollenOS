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
static uintptr_t  g_jumpSpace = 0;

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

static bool
__IsTrampolineCodeValid(void)
{
    // Ensure that the trampoline code length is within valid size bounds and aligned to 8 bytes.
    return __GlbTramplineCode_length >= 32 && 
        __GlbTramplineCode_length <= 0x1000 && 
        (__GlbTramplineCode_length & 7) == 0;
}

static oserr_t
__InitializeApplicationJumpSpace(void)
{
    MemorySpace_t* memorySpace = GetCurrentMemorySpace();
    int            numberOfCores = atomic_load(&GetMachine()->NumberOfCores);
    uintptr_t      page;
    vaddr_t        mapping;
    void*          stacks;
    uint64_t*      parameters;
    oserr_t        status;

    if (numberOfCores <= 1) {
        return OS_ENOTSUPPORTED;
    }

    // CR3 is loaded in protected mode, before long mode is available.
    if (numberOfCores <= 1 || memorySpace->PlatformData.Cr3PhysicalAddress == 0 ||
        (memorySpace->PlatformData.Cr3PhysicalAddress & 0xfff) ||
        (uint64_t)memorySpace->PlatformData.Cr3PhysicalAddress > UINT32_MAX) {
        return OS_ENOTSUPPORTED;
    }

    if (!__IsTrampolineCodeValid()) {
        ERROR("__InitializeApplicationJumpSpace: trampoline code is invalid.");
        return OS_ENOTSUPPORTED;
    }

    // Ensure that we retrieve a <1mb page for the trampoline code.
    status = AllocatePhysicalMemory(MEMORY_MASK_BIOS, 1, &page);
    if (status != OS_EOK) {
        ERROR("__InitializeApplicationJumpSpace: failed to allocate trampoline page");
        return status;
    }

    // This should never happen, but we check anyway 
    // to ensure the trampoline page is below 1MB.
    if (page == 0 || page >= 0x100000) {
        ERROR("__InitializeApplicationJumpSpace: trampoline page is out of bounds: 0x%" PRIxIN "", page);
        FreePhysicalMemory(1, &page);
        return OS_ENOTSUPPORTED;
    }

    stacks = kmalloc((size_t)(numberOfCores - 1) * THREADING_KERNEL_STACK_SIZE);
    if (!stacks) {
        FreePhysicalMemory(1, &page);
        return OS_EOOM;
    }

    // Allocate from firmware-reported free RAM, then explicitly identity map
    // the SIPI page so instruction fetch survives the transition to paging.
    status = MemorySpaceMap(
        memorySpace,
        &(struct MemorySpaceMapOptions) {
            .VirtualStart = page,
            .Pages = &page,
            .Length = 0x1000,
            .Flags = MAPPING_COMMIT | MAPPING_EXECUTABLE | MAPPING_PERSISTENT,
            .PlacementFlags = MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_FIXED
        },
        &mapping
    );
    if (status != OS_EOK) {
        ERROR("__InitializeApplicationJumpSpace: failed to map trampoline page: %u", status);
        kfree(stacks);
        FreePhysicalMemory(1, &page);
        return status;
    }

    // Copy the code directly onto the page, and setup the variable space. This
    // must be aligned with the trampoline code layout.
    memcpy((void*)mapping, __GlbTramplineCode, __GlbTramplineCode_length);
    parameters = (uint64_t*)(mapping + __GlbTramplineCode_length);
    parameters[-1] = (uint64_t)(uintptr_t)SmpApplicationCoreEntry;
    parameters[-2] = (uint64_t)memorySpace->PlatformData.Cr3PhysicalAddress;
    parameters[-3] = (uint64_t)(uintptr_t)stacks + THREADING_KERNEL_STACK_SIZE;
    parameters[-4] = THREADING_KERNEL_STACK_SIZE;

    // Keep the page and stacks alive, including if a target is slow to start.
    // APs atomically claim a stack only after paging has been enabled.
    g_jumpSpace = page;
    TRACE("__InitializeApplicationJumpSpace: page=0x%" PRIxIN ", stacks=%u", page, numberOfCores - 1);
    return OS_EOK;
}

void
StartApplicationCore(
    _In_ SystemCpuCore_t* core)
{
    OSTimestamp_t deadline;
    uuid_t        coreId = CpuCoreId(core);
    int           timeout;

    // Check if the core is already running. If it is, there is no need to start it.
    if (CpuCoreState(core) & CpuStateRunning) {
        return;
    }

    // Initialize jump space
    if (!g_jumpSpace) {
        oserr_t status = __InitializeApplicationJumpSpace();
        if (status != OS_EOK) {
            ERROR(" > failed to prepare AP trampoline: %u", status);
            return;
        }
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
    if (ApicPerformSIPI(coreId, g_jumpSpace) != OS_EOK) {
        ERROR(" > failed to boot core %" PRIuIN " (sipi failed)", coreId);
        return;
    }

    // Wait - check if it booted, give it 10ms
    // If it didn't boot then send another SIPI, and wait for completion.
    timeout = 10;
    SystemTimerGetWallClockTime(&deadline);
    while (!(CpuCoreState(core) & CpuStateRunning) && timeout--) {
        OSTimestampAddNsec(&deadline, &deadline, NSEC_PER_MSEC);
        SystemTimerStall(&deadline);
    }
    
    if (!(CpuCoreState(core) & CpuStateRunning)) {
        ERROR(" > core %" PRIuIN " did not boot within the initial timeout, sending another SIPI", coreId);
        if (ApicPerformSIPI(coreId, g_jumpSpace) != OS_EOK) {
            ERROR(" > failed to boot core %" PRIuIN " (sipi failed)", coreId);
            return;
        }
    }

    // Wait for the core to complete startup, giving it a longer timeout.
    timeout = 1000;
    SystemTimerGetWallClockTime(&deadline);
    while (!(CpuCoreState(core) & CpuStateRunning) && timeout--) {
        OSTimestampAddNsec(&deadline, &deadline, NSEC_PER_MSEC);
        SystemTimerStall(&deadline);
    }
    
    if (!(CpuCoreState(core) & CpuStateRunning)) {
        ERROR(" > core %" PRIuIN " did not complete startup", coreId);
    }
}
