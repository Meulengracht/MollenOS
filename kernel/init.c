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

/* Includes 
 * - System */
#include <revision.h>
#include <mollenos.h>
#include <arch.h>
#include <acpiinterface.h>
#include <garbagecollector.h>
#include <modules/modules.h>
#include <process/phoenix.h>
#include <interrupts.h>
#include <scheduler.h>
#include <threading.h>
#include <timers.h>
#include <vfs\vfs.h>
#include <heap.h>
#include <log.h>

/* Includes
 * - Library */
#include <stddef.h>

/* Print Header Information */
void PrintHeader(MCoreBootInfo_t *BootInfo)
{
	Log("MollenOS - Platform: %s - Version %i.%i.%i",
		ARCHITECTURE_NAME, REVISION_MAJOR, REVISION_MINOR, REVISION_BUILD);
	Log("Written by Philip Meulengracht, Copyright 2011-2016.");
	Log("Bootloader - %s", BootInfo->BootloaderName);
	Log("VC Build %s - %s\n", BUILD_DATE, BUILD_TIME);
}

/* * 
 * Shared Entry in MollenOS
 * */
void MCoreInitialize(MCoreBootInfo_t *BootInfo)
{
	/* Initialize Log */
	LogInit();

	/* Print Header */
	PrintHeader(BootInfo);

	/* Init HAL */
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
	if (ModulesInit(&BootInfo->Descriptor) != OsNoError) {
		Idle();
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
	GcInit();

	/* Initialize the process manager (Phoenix)
	 * We must do this before starting up servers */
	PhoenixInit();
	
	/* Initialize the virtual filesystem 
	 * This should be moved to its own server
	 * at some point.. */
	VfsInit();

	/* Last step, boot up all available system servers
	 * like device-managers, vfs, etc */
	ModulesRunServers();

	/* Enter Idle Loop */
	while (1)
		Idle();
}