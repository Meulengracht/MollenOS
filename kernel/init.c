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
#include <mollenos.h>

#include <system/iospace.h>
#include <system/utils.h>

#include <acpiinterface.h>
#include <garbagecollector.h>
#include <modules/modules.h>
#include <process/phoenix.h>
#include <interrupts.h>
#include <scheduler.h>
#include <threading.h>
#include <timers.h>
#include <debug.h>
#include <heap.h>
#include <log.h>

/* Includes
 * - Library */
#include <stddef.h>

/* PrintHeader
 * Print build information and os-versioning */
void
PrintHeader(
    _In_ MCoreBootInfo_t *BootInfo)
{
	Log("MollenOS - Platform: %s - Version %i.%i.%i",
		ARCHITECTURE_NAME, REVISION_MAJOR, REVISION_MINOR, REVISION_BUILD);
	Log("Written by Philip Meulengracht, Copyright 2011-2016.");
	Log("Bootloader - %s", BootInfo->BootloaderName);
	Log("%s build %s - %s\n", BUILD_SYSTEM, BUILD_DATE, BUILD_TIME);
}

/* MCoreInitialize
 * Main kernel initializor function. Set's up the entire system. */
void
MCoreInitialize(
    _In_ MCoreBootInfo_t *BootInfo)
{
	// Initialize log and print the header
	LogInit();
	PrintHeader(BootInfo);

	// Now call the underlying architecture setup.
	BootInfo->InitHAL(BootInfo->ArchBootInfo, &BootInfo->Descriptor);

	/* Init the heap */
	HeapInit();

	/* The first memory operaiton we will
	 * be performing is upgrading the log away
	 * from the static buffer */
	LogUpgrade(LOG_PREFFERED_SIZE);

	/* Initialize the interrupt/timers sub-system
	 * after we have a heap, so systems can
	 * register interrupts */
	InterruptInitialize();
	TimersInitialize();

	/* We want to initialize IoSpaces as soon
	 * as possible so devices and systems 
	 * can register/claim their io-spaces */
	IoSpaceInitialize();

	/* Parse the ramdisk early, but we don't run
	 * servers yet, this is not needed, but since there
	 * is no dependancies yet, just do it */
	if (ModulesInitialize(&BootInfo->Descriptor) != OsSuccess) {
		CpuIdle();
	}

	/* Init Threading & Scheduler for boot cpu */
	SchedulerInit(0);
	ThreadingInitialize(0);

	/* Now we can do some early ACPI
	 * initialization if ACPI is present
	 * on this system */
	AcpiEnumerate();

	/* Now we initialize some systems that 
	 * rely on the presence of ACPI tables
	 * or the absence of them */
	BootInfo->InitPostSystems();

	/* Now we finish the ACPI setup IF 
	 * ACPI is present on the system */
	if (AcpiAvailable() == ACPI_AVAILABLE) {
		AcpiInitialize();
		AcpiScan();
	}

	/* Initialize the GC 
	 * It recycles threads, ashes and 
	 * keeps the heap clean ! */
	GcInitialize();

	/* Initialize the process manager (Phoenix)
	 * We must do this before starting up servers */
	PhoenixInit();

	// Last step, boot up all available system servers
	// like device-managers, vfs, etc
	ModulesRunServers();
	while (1) {
        TRACE("End of initialization");
		CpuIdle();
    }
}
