/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
#define __MODULE "INIT"
#define __TRACE

/* Includes 
 * - System */
#include <revision.h>
#include <system/setup.h>
#include <system/iospace.h>
#include <system/utils.h>
#include <machine.h>

#include <acpiinterface.h>
#include <garbagecollector.h>
#include <modules/modules.h>
#include <process/phoenix.h>
#include <interrupts.h>
#include <scheduler.h>
#include <threading.h>
#include <timers.h>
#include <crc32.h>
#include <video.h>
#include <debug.h>
#include <arch.h>
#include <heap.h>
#include <log.h>

/* Includes
 * - Library */
#include <stddef.h>

/* Globals
 * - Static state variables */
static SystemMachine_t Machine = { 
    { 0 }, { 0 }, { 0 },        // Strings
    REVISION_MAJOR, REVISION_MINOR, REVISION_BUILD,
    { 0 },                      // BootInformation
    COLLECTION_INIT(KeyInteger) // SystemDomains
};

/* GetMachine
 * Retrieves a pointer for the machine structure. */
SystemMachine_t*
GetMachine(void)
{
    return &Machine;
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

/* MCoreInitialize
 * Callable by the architecture layer to initialize the kernel */
void
MCoreInitialize(
    _In_ Multiboot_t *BootInformation)
{
    // Variables
    Flags_t SystemsInitialized  = 0;
    Flags_t SystemsAvailable    = 0;
    
    // Initialize all our static memory systems and global variables
    memcpy(&Machine.BootInformation, BootInformation, sizeof(Multiboot_t));
    Crc32GenerateTable();
    LogInitialize();
    
    // Initialize the domain
    InitializePrimaryDomain();

    // Print build/info-header
    PrintHeader(&Machine.BootInformation);

    // Query available systems
    SystemFeaturesQuery(&Machine.BootInformation, &SystemsAvailable);
    TRACE("Supported features: 0x%x", SystemsAvailable);

    // Initialize memory systems
    if (SystemsAvailable & SYSTEM_FEATURE_MEMORY) {
        TRACE("Running SYSTEM_FEATURE_MEMORY");
        SystemFeaturesInitialize(&Machine.BootInformation, SYSTEM_FEATURE_MEMORY);
        DebugInstallPageFaultHandlers();
    }

    // Initialize our kernel heap
    HeapConstruct(HeapGetKernel(), MEMORY_LOCATION_HEAP, MEMORY_LOCATION_HEAP_END, 0);

    // Don't access video before after memory access
    if (SystemsAvailable & SYSTEM_FEATURE_OUTPUT) {
        VideoInitialize();
    }

    // Parse the ramdisk as early as possible, so right
    // after upgrading log and having heap
    if (ModulesInitialize(&Machine.BootInformation) != OsSuccess) {
        ERROR("Failed to read the ramdisk supplied with the OS.");
        CpuIdle();
    }
    
    // Now that we have an allocation system add all initializors
    // that need dynamic memory here
    GcConstruct();
    ThreadingInitialize();
    SchedulerInitialize();
    ThreadingEnable();
    IoSpaceInitialize();

    // Run early ACPI initialization if available
    // we will need table access maybe
    if (AcpiInitializeEarly() == OsSuccess) {
        SystemsInitialized |= SYSTEM_FEATURE_ACPI;
    }

    // Initiate interrupt support now that systems are up and running
    if (SystemsAvailable & SYSTEM_FEATURE_INTERRUPTS) {
        TRACE("Running SYSTEM_FEATURE_INTERRUPTS");
        SystemFeaturesInitialize(&Machine.BootInformation, SYSTEM_FEATURE_INTERRUPTS);
    }

    // Don't spawn threads before after interrupts
    LogInitializeFull();

    // Perform the full acpi initialization sequence
    if (AcpiAvailable() == ACPI_AVAILABLE) {
        AcpiInitialize();
        if (AcpiDevicesScan() != AE_OK) {
            ERROR("Failed to finalize the ACPI setup.");
            CpuIdle();
        }
    }

    // Initialize all subsystems that spawn threads
    // as almost everything is up and running at this point
    GcInitialize();
    PhoenixInitialize();
    
    // Run system finalization before we spawn processes
    if (SystemsAvailable & SYSTEM_FEATURE_FINALIZE) {
        TRACE("Running SYSTEM_FEATURE_FINALIZE");
        SystemFeaturesInitialize(&Machine.BootInformation, SYSTEM_FEATURE_FINALIZE);
    }

    // Last step, boot up all available system servers
    // like device-managers, vfs, etc
    ModulesRunServers();
    
    // Enter idle loop.
    WARNING("End of initialization");
    while (1) {
        CpuIdle();
    }
}
