/**
 * Copyright 2018, Philip Meulengracht
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
 */

#define __MODULE "MACH"
#define __TRACE

#include <assert.h>
#include <arch/interrupts.h>
#include <arch/io.h>
#include <arch/platform.h>
#include <arch/thread.h>
#include <arch/utils.h>
#include <machine.h>

// Include the private structure for PerCpu data
#include "components/cpu_private.h"

#include <acpiinterface.h>
#include <console.h>
#include <crc32.h>
#include <debug.h>
#include <futex.h>
#include <handle.h>
#include <handle_set.h>
#include <hpet.h>
#include <interrupts.h>
#include <scheduler.h>
#include <stdio.h>
#include <threading.h>
#include <userevent.h>

extern void SpawnBootstrapper(void);

static SystemMachine_t g_machine = {
    { 0 }, { 0 }, { 0 },                        // Strings
    { 0 }, SYSTEM_CPU_INIT, { 0 }, { 0 },              // BootInformation, Processor, MemorySpace, PhysicalMemory
    { 0 }, { { 0 } }, LIST_INIT, // GAMemory, Memory Map, SystemDomains
    NULL, 0, NULL,                                     // InterruptControllers
    SYSTEM_TIMERS_INIT,                                     // SystemTimers
    ATOMIC_VAR_INIT(1), ATOMIC_VAR_INIT(1), 
    ATOMIC_VAR_INIT(1), 0, 0, 0 // Total Information
};
static SystemCpuCore_t g_bootCore = { 0 };

SystemMachine_t*
GetMachine(void)
{
    return &g_machine;
}

static void __DumpVBoot(
        _In_ struct VBoot* bootInformation)
{
    TRACE("Dumping VBOOT Information:");
    TRACE("---------------------------------------");
    TRACE("Magic:            0x%x",   bootInformation->Magic);
    TRACE("Version:          0x%x",   bootInformation->Version);
    TRACE("Firmware:         0x%x",   bootInformation->Firmware);
    TRACE("ConfigTableCount: 0x%x",   bootInformation->ConfigurationTableCount);
    TRACE("ConfigTable:      0x%llx", bootInformation->ConfigurationTable);
    TRACE("Kernel.Base       0x%llx", bootInformation->Kernel.Base);
    TRACE("Kernel.Length     0x%x",   bootInformation->Kernel.Length);
    TRACE("Ramdisk.Data      0x%llx", bootInformation->Ramdisk.Data);
    TRACE("Ramdisk.Length    0x%x",   bootInformation->Ramdisk.Length);
    TRACE("Phoenix.Base      0x%llx", bootInformation->Phoenix.Base);
    TRACE("Phoenix.Length    0x%llx", bootInformation->Phoenix.Length);
    TRACE("---------------------------------------");
}

_Noreturn void
InitializeMachine(
    _In_ struct VBoot* bootInformation)
{
    oserr_t oserr;

    // Initialize all our static memory systems and global variables
    LogInitialize();
    Crc32GenerateTable();
    FutexInitialize();

    // Boot information must be supplied
    TRACE("InitializeMachine(bootInformation=0x%x)", bootInformation);
    if (bootInformation == NULL) {
        for(;;) {
            ERROR("InitializeMachine bootInformation was NULL");
            ArchProcessorHalt();
        }
    }

    __DumpVBoot(bootInformation);
    sprintf(&g_machine.Architecture[0], "Architecture: %s", ARCHITECTURE_NAME);
    sprintf(&g_machine.Author[0],       "Philip Meulengracht, Copyright 2011.");
    sprintf(&g_machine.Date[0],         "%s - %s", __DATE__, __TIME__);
    memcpy(&g_machine.BootInformation, bootInformation, sizeof(struct VBoot));

    // Initialize the processor structure and the underlying platform. This is called before any
    // memory is taken care of, which means VBoot environment where all physical memory is present.
    CpuInitializePlatform(&g_machine.Processor, &g_bootCore);

    // Initialize memory environment. This should enable and initialize all forms of memory management
    // and should leave the system ready to allocate memory at will. After this call Per-Core memory
    // should also be set up
    oserr = MachineMemoryInitialize(&g_machine, &g_bootCore);
    if (oserr != OS_EOK) {
        ERROR("Failed to initalize system memory system");
        ArchProcessorHalt();
    }

    oserr = ConsoleInitialize();
    if (oserr != OS_EOK) {
        ERROR("Failed to initialize output for system.");
        ArchProcessorHalt();
    }

    // Build system topology by enumerating the SRAT table if present.
    // If ACPI is not present or the SRAT is missing the system is running in UMA
    // mode and there is no hardware seperation
#ifdef __OSCONFIG_ACPI_SUPPORT
    oserr = AcpiInitializeEarly();
    if (oserr != OS_EOK) {
        // Assume UMA machine and put the machine into UMA modKERNELAPI e
        SetMachineUmaMode();
    }
#else
    SetMachineUmaMode();
#endif

    // Create the rest of the OS systems
    LogInitializeFull();
    oserr = InitializeHandles();
    if (oserr != OS_EOK) {
        ERROR("Failed to initialize the handle subsystem.");
        ArchProcessorHalt();
    }

    oserr = HandleSetsInitialize();
    if (oserr != OS_EOK) {
        ERROR("Failed to initialize the handle set subsystem.");
        ArchProcessorHalt();
    }

    // initialize the idle thread for this core
    ThreadingEnable(&g_bootCore);

    // initialize the interrupt subsystem
    InitializeInterruptTable();
    InitializeInterruptHandlers();
    oserr = PlatformInterruptInitialize();
    if (oserr != OS_EOK) {
        ERROR("Failed to initialize interrupts for system.");
        ArchProcessorHalt();
    }

    // Initialize all platform timers. Ok so why tho? Timers are a part of the kernel in
    // vali, as the only form for drivers. This is because the kernel relies on time-management
    // in some form, and thus to have performance atleast so-so we keep those drivers here. One could
    // argue we should move them out, but I haven't prioritized this.
#ifdef __OSCONFIG_ACPI_SUPPORT
    if (AcpiAvailable() == ACPI_AVAILABLE) {
        // There is no return code here because to be honest we don't really care
        // if the HPET is present or not. If it is, great, otherwise wow bad platform.
        HpetInitialize();
    }
#endif
    oserr = PlatformTimersInitialize();
    if (oserr != OS_EOK) {
        ERROR("Failed to initialize timers for system.");
        ArchProcessorHalt();
    }

    // Spawn the time synchronizer after initiating the timer subsystem. This should run in a separate thread
    // as it will wait up to 1 second (worst-case) before being able to synchronize clock sources.
    oserr = SystemSynchronizeTimeSources();
    if (oserr != OS_EOK) {
        ERROR("Failed to spawn time sync thread: %u", oserr);
        ArchProcessorHalt();
    }

    // The handle janitor, which is simply just a thread waiting for handles to destroy, is only made this
    // way because of threads. Threads are like dirty teenagers refusing to take a bath, so we have to clean
    // them when they aren't active. So we clean them in a seperate thread, and as threads are handles, we
    // simply invest in a janitor to clean.
    oserr = InitializeHandleJanitor();
    if (oserr != OS_EOK) {
        ERROR("Failed to initialize system janitor.");
        ArchProcessorHalt();
    }

    // Perform the full acpi initialization sequence. This should not be a part of the kernel
    // and should be a seperate driver module. We only need the table-parsing capability of ACPICA in
    // the kernel to discover system metrics/configuration, but the entire ACPICA initialization should
    // be out of the kernel.
    // TODO move this out of kernel some day
#ifdef __OSCONFIG_ACPI_SUPPORT
    if (AcpiAvailable() == ACPI_AVAILABLE) {
        AcpiInitialize();
        if (AcpiDevicesScan() != AE_OK) {
            ERROR("Failed to finalize the ACPI setup.");
            ArchProcessorHalt();
        }
    }
#endif

#ifdef __OSCONFIG_ENABLE_MULTIPROCESSORS
    CpuEnableMultiProcessorMode();
#endif

    // Initialize all userspace subsystems here
    UserEventInitialize();

    // Start the bootstrap module if present
    SpawnBootstrapper();

    // yield before going to assume new threads
    TRACE("End of initialization, yielding control");
    SchedulerEnable();
    ArchThreadYield();

    for (;;) {
        ArchProcessorIdle();
    }
}

oserr_t
AllocatePhysicalMemory(
        _In_ size_t     pageMask,
        _In_ int        pageCount,
        _In_ uintptr_t* pages)
{
    SystemMemoryAllocatorRegion_t* region;
    oserr_t                        osStatus;
    int                            pagesLeftToAllocate = pageCount;
    int                            i = GetMachine()->PhysicalMemory.MaskCount - 1;

    // default to the highest allocator
    region = &GetMachine()->PhysicalMemory.Region[i];
    while (pagesLeftToAllocate > 0 && i >= 0) {
        int pagesAllocated = pagesLeftToAllocate;

        // make sure pagemask is correct if one is provided
        if (pageMask) {
            if (pageMask < GetMachine()->PhysicalMemory.Masks[i]) {
                region = &GetMachine()->PhysicalMemory.Region[--i];
                continue;
            }
        }

        // try to allocate all neccessary pages from this memory mask allocator
        SpinlockAcquireIrq(&region->Lock);
        osStatus = MemoryStackPop(&region->Stack, &pagesAllocated, pages);
        SpinlockReleaseIrq(&region->Lock);

        // if it returns out of memory, then no pages are available here
        if (osStatus != OS_EOOM) {
            // otherwise, we subtract the number of pages allocated from this
            pagesLeftToAllocate -= pagesAllocated;
        }

        // go to next allocator
        region = &GetMachine()->PhysicalMemory.Region[--i];
    }

    if (osStatus == OS_EOK) {
        GetMachine()->NumberOfFreeMemoryBlocks -= (size_t)pageCount;
    }
    return osStatus;
}

void
FreePhysicalMemory(
        _In_ int              pageCount,
        _In_ const uintptr_t* pages)
{
    SystemMemoryAllocatorRegion_t* region = NULL;
    int                            i;

    for (i = 0; i < pageCount; i++) {
        uintptr_t address = pages[i];
        assert(address != 0);

        for (int j = 0; j < GetMachine()->PhysicalMemory.MaskCount; j++) {
            if (address < GetMachine()->PhysicalMemory.Masks[j]) {
                region = &GetMachine()->PhysicalMemory.Region[j];
                break;
            }
        }

        if (region == NULL) {
            // Tring to free an invalid address
            ERROR("FreePhysicalMemory tried to free an invalid page");
            ERROR("FreePhysicalMemory pageCount=%i, i=%i", pageCount, i);
            ERROR("FreePhysicalMemory PAGES:");
            for (int j = 0; j < pageCount; j++) {
                ERROR("FreePhysicalMemory 0x%llx", pages[j]);
            }
            assert(0);
        }

        SpinlockAcquireIrq(&region->Lock);
        MemoryStackPush(&region->Stack, address, 1);
        SpinlockReleaseIrq(&region->Lock);
    }

    GetMachine()->NumberOfFreeMemoryBlocks += (size_t)pageCount;
}
