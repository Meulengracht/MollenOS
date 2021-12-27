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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 */
#define __MODULE "MACH"
#define __TRACE

#include <arch.h>
#include <arch/interrupts.h>
#include <arch/thread.h>
#include <arch/utils.h>
#include <machine.h>

#include <acpiinterface.h>
#include <console.h>
#include <crc32.h>
#include <debug.h>
#include <futex.h>
#include <handle.h>
#include <handle_set.h>
#include <interrupts.h>
#include <scheduler.h>
#include <stdio.h>
#include <threading.h>
#include <timers.h>
#include <userevent.h>
#include "arch/io.h"

extern void SpawnBootstrapper(void);

static SystemMachine_t Machine = { 
    { 0 }, { 0 }, { 0 },                        // Strings
    { 0 }, SYSTEM_CPU_INIT, { 0 }, { 0 },              // BootInformation, Processor, MemorySpace, PhysicalMemory
    { 0 }, { { 0 } }, LIST_INIT, // GAMemory, Memory Map, SystemDomains
    NULL, 0, NULL,                                     // InterruptControllers
    { { { 0 } } },                                     // SystemTime
    ATOMIC_VAR_INIT(1), ATOMIC_VAR_INIT(1), 
    ATOMIC_VAR_INIT(1), 0, 0, 0 // Total Information
};

SystemMachine_t*
GetMachine(void)
{
    return &Machine;
}

_Noreturn void
InitializeMachine(
    _In_ struct VBoot* bootInformation)
{
    OsStatus_t osStatus;

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
    sprintf(&Machine.Architecture[0], "Architecture: %s", ARCHITECTURE_NAME);
    sprintf(&Machine.Author[0],       "Philip Meulengracht, Copyright 2011.");
    sprintf(&Machine.Date[0],         "%s - %s", __DATE__, __TIME__);
    memcpy(&Machine.BootInformation, bootInformation, sizeof(struct VBoot));

    InitializePrimaryProcessor(&Machine.Processor);

    // Initialize machine memory
    osStatus = MachineInitializeMemorySystems(&Machine);
    if (osStatus != OsSuccess) {
        ERROR("Failed to initalize system memory system");
        ArchProcessorHalt();
    }

    osStatus = InitializeConsole();
    if (osStatus != OsSuccess) {
        ERROR("Failed to initialize output for system.");
        ArchProcessorHalt();
    }

    // Build system topology by enumerating the SRAT table if present.
    // If ACPI is not present or the SRAT is missing the system is running in UMA
    // mode and there is no hardware seperation
#ifdef __OSCONFIG_ACPI_SUPPORT
    osStatus = AcpiInitializeEarly();
    if (osStatus != OsSuccess) {
        // Assume UMA machine and put the machine into UMA modKERNELAPI e
        SetMachineUmaMode();
    }
#else
    SetMachineUmaMode();
#endif

    // Create the rest of the OS systems
    osStatus = InitializeHandles();
    if (osStatus != OsSuccess) {
        ERROR("Failed to initialize the handle subsystem.");
        ArchProcessorIdle();
    }

    HandleSetsInitialize();
    ThreadingEnable();
    InitializeInterruptTable();
    InitializeInterruptHandlers();
    osStatus = InterruptInitialize();
    if (osStatus != OsSuccess) {
        ERROR("Failed to initialize interrupts for system.");
        ArchProcessorIdle();
    }
    LogInitializeFull();

    osStatus = InitializeHandleJanitor();
    if (osStatus != OsSuccess) {
        ERROR("Failed to initialize system janitor.");
        ArchProcessorIdle();
    }

    // Perform the full acpi initialization sequence
#ifdef __OSCONFIG_ACPI_SUPPORT
    if (AcpiAvailable() == ACPI_AVAILABLE) {
        AcpiInitialize();
        if (AcpiDevicesScan() != AE_OK) {
            ERROR("Failed to finalize the ACPI setup.");
            ArchProcessorIdle();
        }
    }
#endif

    // Last step is to enable timers that kickstart all the other threads
    osStatus = InitializeSystemTimers();
    if (osStatus != OsSuccess) {
        ERROR("Failed to initialize timers for system.");
        ArchProcessorHalt();
    }
    TimersSynchronizeTime();
#ifdef __OSCONFIG_ENABLE_MULTIPROCESSORS
    EnableMultiProcessoringMode();
#endif

    // Initialize all userspace subsystems here
    UserEventInitialize();

    // Start the bootstrap module if present
    SpawnBootstrapper();

    // yield before going to assume new threads
    WARNING("End of initialization, yielding control");
    ThreadingYield();
    goto IdleProcessor;

IdleProcessor:
    while (1) {
        ArchProcessorIdle();
    }
}

OsStatus_t
AllocatePhysicalMemory(
        _In_ size_t     pageMask,
        _In_ int        pageCount,
        _In_ uintptr_t* pages)
{
    SystemMemoryAllocatorRegion_t* region;
    OsStatus_t                     osStatus;
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
        IrqSpinlockAcquire(&region->Lock);
        osStatus = MemoryStackPop(&region->Stack, &pagesAllocated, pages);
        IrqSpinlockRelease(&region->Lock);

        // if it returns out of memory, then no pages are available here
        if (osStatus != OsOutOfMemory) {
            // otherwise, we subtract the number of pages allocated from this
            pagesLeftToAllocate -= pagesAllocated;
        }

        // go to next allocator
        region = &GetMachine()->PhysicalMemory.Region[--i];
    }

    if (osStatus == OsSuccess) {
        GetMachine()->NumberOfFreeMemoryBlocks -= (size_t)pageCount;
    }
    return osStatus;
}

void
FreePhysicalMemory(
        _In_ int              pageCount,
        _In_ const uintptr_t* pages)
{
    SystemMemoryAllocatorRegion_t* region;
    int                            i;

    for (i = 0; i < pageCount; i++) {
        uintptr_t address = pages[i];
        for (int j = GetMachine()->PhysicalMemory.MaskCount - 1; j >= 0; j--) {
            if (address >= GetMachine()->PhysicalMemory.Masks[j]) {
                region = &GetMachine()->PhysicalMemory.Region[j];
            }
        }

        IrqSpinlockAcquire(&region->Lock);
        MemoryStackPush(&region->Stack, address, 1);
        IrqSpinlockRelease(&region->Lock);
    }

    GetMachine()->NumberOfFreeMemoryBlocks += (size_t)pageCount;
}
