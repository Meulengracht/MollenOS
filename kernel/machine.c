/**
 * MollenOS
 *
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
 *
 * Kernel entry point
 */
#define __MODULE "MACH"
#define __TRACE

#include <arch.h>
#include <arch/interrupts.h>
#include <arch/thread.h>
#include <arch/utils.h>
#include <revision.h>
#include <machine.h>

#include <acpiinterface.h>
#include <console.h>
#include <crc32.h>
#include <debug.h>
#include <futex.h>
#include <modules/ramdisk.h>
#include <modules/manager.h>
#include <handle.h>
#include <handle_set.h>
#include <heap.h>
#include <interrupts.h>
#include <scheduler.h>
#include <stdio.h>
#include <threading.h>
#include <timers.h>
#include <userevent.h>

#ifdef __OSCONFIG_TEST_KERNEL
extern void StartTestingPhase(void);
#endif

static SystemMachine_t Machine = { 
    { 0 }, { 0 }, { 0 },                        // Strings
    REVISION_MAJOR, REVISION_MINOR, REVISION_BUILD,
    { 0 }, SYSTEM_CPU_INIT, { 0 }, { 0 },              // BootInformation, Processor, MemorySpace, PhysicalMemory
    OS_IRQ_SPINLOCK_INIT, { 0 }, { { 0 } }, LIST_INIT, // GAMemory, Memory Map, SystemDomains
    NULL, 0, NULL,                                     // InterruptControllers
    { { { 0 } } },                                     // SystemTime
    ATOMIC_VAR_INIT(1), ATOMIC_VAR_INIT(1), 
    ATOMIC_VAR_INIT(1), 0, 0                           // Total Information
};

SystemMachine_t*
GetMachine(void)
{
    return &Machine;
}

void
PrintHeader(
    _In_ struct VBoot* bootInformation)
{
    WRITELINE("MollenOS - Platform: %s - Version %" PRIiIN ".%" PRIiIN ".%" PRIiIN "",
        ARCHITECTURE_NAME, REVISION_MAJOR, REVISION_MINOR, REVISION_BUILD);
    WRITELINE("Written by Philip Meulengracht, Copyright 2011.");
    WRITELINE("%s build %s - %s\n", BUILD_SYSTEM, BUILD_DATE, BUILD_TIME);
}

_Noreturn void
InitializeMachine(
    _In_ struct VBoot* bootInformation)
{
    OsStatus_t osStatus;

    // Boot information must be supplied
    if (bootInformation == NULL) {
        for(;;) {
            // @todo perform unique halt/set error
            ArchProcessorHalt();
        }
    }
    
    // Initialize all our static memory systems and global variables
    memcpy(&Machine.BootInformation, bootInformation, sizeof(struct VBoot));
    Crc32GenerateTable();
    LogInitialize();
    FutexInitialize();

    sprintf(&Machine.Architecture[0], "System: %s", ARCHITECTURE_NAME);
    sprintf(&Machine.Author[0],       "Philip Meulengracht, Copyright 2011.");
    sprintf(&Machine.Date[0],         "%s - %s", BUILD_DATE, BUILD_TIME);

    InitializePrimaryProcessor(&Machine.Processor);

    // Print build/info-header
    PrintHeader(&Machine.BootInformation);

    // Initialize machine memory
    osStatus = InitializeSystemMemory(&Machine.BootInformation, &Machine.PhysicalMemory,
                                      &Machine.GlobalAccessMemory, &Machine.MemoryMap, &Machine.MemoryGranularity,
                                      &Machine.NumberOfMemoryBlocks);
    if (osStatus != OsSuccess) {
        ERROR("Failed to initalize system memory system");
        ArchProcessorHalt();
    }

#ifdef __OSCONFIG_HAS_MMIO
    osStatus = InitializeMemorySpace(&Machine.SystemSpace);
    if (osStatus != OsSuccess) {
        ERROR("Failed to initalize system memory space");
        goto StopAndShowError;
    }
#else
#error "Kernel does not support non-mmio platforms"
#endif
    MemoryCacheInitialize();
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

    // Last step is to enable timers that kickstart all other threads
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

    // Either of three things happen, testing phase can begin, we can enter
    // debug console or last option is normal operation.
#ifdef __OSCONFIG_TEST_KERNEL
    StartTestingPhase();
#else
    osStatus = ParseInitialRamdisk(&Machine.BootInformation);
    if (osStatus != OsSuccess) {
        ERROR(" > no ramdisk provided, operating system stopping");
        ArchProcessorHalt();
    }
    SpawnServices();
#endif

    // yield before going to assume new threads
    WARNING("End of initialization, yielding control");
    ThreadingYield();
    goto IdleProcessor;

StopAndShowError:
    osStatus = InitializeConsole();
    if (osStatus != OsSuccess) {
        ERROR("Failed to initialize output for system.");
        ArchProcessorHalt();
    }

IdleProcessor:
    while (1) {
        ArchProcessorIdle();
    }
}

OsStatus_t
AllocatePhysicalMemory(
    _In_ int        PageCount,
    _In_ uintptr_t* Pages)
{
    OsStatus_t Status = OsSuccess;
    int        PagesAllocated;

    IrqSpinlockAcquire(&GetMachine()->PhysicalMemoryLock);
    PagesAllocated = bounded_stack_pop_multiple(&GetMachine()->PhysicalMemory, (void**)&Pages[0], PageCount);
    if (PagesAllocated < PageCount) {
        bounded_stack_push_multiple(&GetMachine()->PhysicalMemory, (void**)&Pages[0], PagesAllocated);
        Status = OsOutOfMemory;
    }
    IrqSpinlockRelease(&GetMachine()->PhysicalMemoryLock);

    return Status;
}