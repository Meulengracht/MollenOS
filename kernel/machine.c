/* MollenOS
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
 * MollenOS Common Entry Point
 */
#define __MODULE "MACH"
#define __TRACE

#include <system/interrupts.h>
#include <system/utils.h>
#include <revision.h>
#include <machine.h>

#include <acpiinterface.h>
#include <garbagecollector.h>
#include <modules/modules.h>
#include <process/phoenix.h>
#include <interrupts.h>
#include <scheduler.h>
#include <threading.h>
#include <timers.h>
#include <stdio.h>
#include <crc32.h>
#include <video.h>
#include <debug.h>
#include <arch.h>
#include <heap.h>

#ifdef __OSCONFIG_TEST_KERNEL
extern void StartTestingPhase(void);
#endif

/* Globals
 * - Static state variables */
static SystemMachine_t Machine = { 
    { 0 }, { 0 }, { 0 }, { 0 },         // Strings
    REVISION_MAJOR, REVISION_MINOR, REVISION_BUILD,
    { 0 }, { { 0 } }, { 0 }, { 0 },     // BootInformation, Processor, MemorySpace, PhysicalMemory
    { { 0 } }, COLLECTION_INIT(KeyInteger), // Memory Map, SystemDomains
    NULL, 0, NULL, NULL,                // InterruptControllers
    0, 0, 0, 0                          // Total Information
};

/* GetMachine
 * Retrieves a pointer for the machine structure. */
SystemMachine_t*
GetMachine(void)
{
    return &Machine;
}

/* EnableMultiProcessoringMode
 * If multiple cores are present this will boot them up. If multiple domains are present
 * it will boot all primary cores in each domain, then boot up rest of cores in our own domain. */
void
EnableMultiProcessoringMode(void)
{
    SystemDomain_t *CurrentDomain = GetCurrentDomain();
    SystemDomain_t *Domain;
    int i;

    // Boot all cores in our own domain, then boot the initial core
    // for all the other domains, they will boot up their own domains.
    foreach (DomainNode, GetDomains()) {
        Domain = (SystemDomain_t*)DomainNode->Data;
        if (Domain != CurrentDomain) {
            StartApplicationCore(&Domain->CoreGroup.PrimaryCore);
        }
    }

    if (CurrentDomain != NULL) {
        // Don't ever include ourself
        for (i = 0; i < (CurrentDomain->CoreGroup.NumberOfCores - 1); i++) {
            StartApplicationCore(&CurrentDomain->CoreGroup.ApplicationCores[i]);
        }
    }
    else {
        // No domains in system - boot all cores except ourself
        for (i = 0; i < (GetMachine()->Processor.NumberOfCores - 1); i++) {
            StartApplicationCore(&GetMachine()->Processor.ApplicationCores[i]);
        }
    }
}

/* PrintHeader
 * Print build information and os-versioning */
void
PrintHeader(
    _In_ Multiboot_t *BootInformation)
{
    WRITELINE("MollenOS - Platform: %s - Version %i.%i.%i",
        ARCHITECTURE_NAME, REVISION_MAJOR, REVISION_MINOR, REVISION_BUILD);
    WRITELINE("Written by Philip Meulengracht, Copyright 2011-2018.");
    WRITELINE("Bootloader - %s", (char*)(uintptr_t)BootInformation->BootLoaderName);
    WRITELINE("%s build %s - %s\n", BUILD_SYSTEM, BUILD_DATE, BUILD_TIME);
}

/* InitializeMachine
 * Callable by the architecture layer to initialize the kernel */
void
InitializeMachine(
    _In_ Multiboot_t *BootInformation)
{
    // Variables
    OsStatus_t Status;
    
    // Initialize all our static memory systems and global variables
    memcpy(&Machine.BootInformation, BootInformation, sizeof(Multiboot_t));
    Crc32GenerateTable();
    LogInitialize();
    GcConstruct();

    // Setup strings
    sprintf(&Machine.Architecture[0],   "System: %s", ARCHITECTURE_NAME);
    sprintf(&Machine.Bootloader[0],     "Boot: %s", (char*)(uintptr_t)BootInformation->BootLoaderName);
    sprintf(&Machine.Author[0],         "Philip Meulengracht, Copyright 2011-2018.");
    sprintf(&Machine.Date[0],           "%s - %s", BUILD_DATE, BUILD_TIME);
    
    // Set initial stats for this machine and then initialize cpu
    InitializeProcessor(&Machine.Processor);
    RegisterStaticCore(&Machine.Processor.PrimaryCore);
    Machine.NumberOfActiveCores = 1;
    Machine.NumberOfProcessors  = 1;
    
    // Print build/info-header
    PrintHeader(&Machine.BootInformation);

    // Initialize machine memory
    Status = InitializeSystemMemory(&Machine.BootInformation, &Machine.PhysicalMemory,
        &Machine.MemoryMap, &Machine.MemoryGranularity, &Machine.NumberOfMemoryBlocks);
    if (Status != OsSuccess) {
        ERROR("Failed to initalize system memory system");
        goto StopAndShowError;
    }

#ifdef __OSCONFIG_HAS_MMIO
    DebugInstallPageFaultHandlers(&Machine.MemoryMap);
    Status = InitializeSystemMemorySpace(&Machine.SystemSpace);
    if (Status != OsSuccess) {
        ERROR("Failed to initalize system memory space");
        goto StopAndShowError;
    }
#else
#error "Kernel does not support non-mmio platforms"
#endif
    Status = HeapConstruct(HeapGetKernel(), Machine.MemoryMap.SystemHeap.Start, 
        Machine.MemoryMap.SystemHeap.Start + Machine.MemoryMap.SystemHeap.Length, 0);
    if (Status != OsSuccess) {
        ERROR("Failed to initalize system heap in range 0x%x => 0x%x", 
            Machine.MemoryMap.SystemHeap.Start, Machine.MemoryMap.SystemHeap.Start + Machine.MemoryMap.SystemHeap.Length);
        goto StopAndShowError;
    }

#ifdef __OSCONFIG_HAS_VIDEO
    Status = VideoInitialize();
    if (Status != OsSuccess) {
        // @todo how the hell do we deal with this
    }
#elif __OSCONFIG_HAS_UART
    // Status = UartInitialize();
#else
#warning "No way of outputting to user on this platform."
#endif

    // Build system topology by enumerating the SRAT table if present.
    // If ACPI is not present or the SRAT is missing the system is running in UMA
    // mode and there is no hardware seperation
#ifdef __OSCONFIG_ACPI_SUPPORT
    Status = AcpiInitializeEarly();
    if (Status != OsSuccess) {
        // Assume UMA machine and put the machine into UMA mode
        SetMachineUmaMode();
    }
#else
    SetMachineUmaMode();
#endif

    // Create the rest of the OS systems
    Status = ThreadingInitialize();
    if (Status != OsSuccess) {
        ERROR("Failed to initialize threading for boot core.");
        CpuIdle();
    }

    Status = ThreadingEnable();
    if (Status != OsSuccess) {
        ERROR("Failed to enable threading for boot core.");
        CpuIdle();
    }

    InitializeInterruptTable();
    Status = InterruptInitialize();
    if (Status != OsSuccess) {
        ERROR("Failed to initialize interrupts for system.");
        CpuIdle();
    }
    LogInitializeFull();

    // Perform the full acpi initialization sequence
#ifdef __OSCONFIG_ACPI_SUPPORT
    if (AcpiAvailable() == ACPI_AVAILABLE) {
        AcpiInitialize();
        if (AcpiDevicesScan() != AE_OK) {
            ERROR("Failed to finalize the ACPI setup.");
            CpuIdle();
        }
    }
#endif

    // Initialize process and usermode systems
    GcInitialize();
    PhoenixInitialize();

    // Last step is to enable timers that kickstart all other threads
    Status = InitializeSystemTimers();
    if (Status != OsSuccess) {
        ERROR("Failed to initialize timers for system.");
        CpuHalt();
    }
#ifdef __OSCONFIG_ENABLE_MULTIPROCESSORS
    EnableMultiProcessoringMode();
#endif

    // Either of three things happen, testing phase can begin, we can enter
    // debug console or last option is normal operation.
#ifdef __OSCONFIG_TEST_KERNEL
    StartTestingPhase();
#else
    Status = ModulesInitialize(&Machine.BootInformation);
    if (Status != OsSuccess) {
        ERROR(" > no ramdisk provided, operating system will enter debug mode");
    }

    ModulesRunServers();
#endif
    WARNING("End of initialization");
    goto IdleProcessor;

StopAndShowError:
#ifdef __OSCONFIG_HAS_VIDEO
    Status = VideoInitialize();
#elif __OSCONFIG_HAS_UART
    // Status = UartInitialize();
#else
#warning "No way of outputting errors to user on this platform."
#endif

IdleProcessor:
    while (1) {
        CpuIdle();
    }
}
