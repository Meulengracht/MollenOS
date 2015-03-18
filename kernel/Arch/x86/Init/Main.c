/* MollenOS
 *
 * Copyright 2011 - 2014, Philip Meulengracht
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
 * MollenOS x86-32 Init Code.
 */

/* Includes */
#include <revision.h>
#include <Arch.h>
#include <LApic.h>
#include <Multiboot.h>
#include <Gdt.h>
#include <Thread.h>
#include <Scheduler.h>
#include <Idt.h>
#include <Cpu.h>
#include <Exceptions.h>

#include <stddef.h>
#include <stdio.h>

/* Extern, this function is declared in the MCore project
 * and all platform libs should enter this function */
extern void mcore_entry(void*);

/* These functions are cross-platform and located in the
 * MCore project. All platform libs should call these 
 * whenever possible. */
extern void HeapInit(void);
extern void ApicPrintCpuTicks(void);

void init(Multiboot_t *BootInfo, uint32_t KernelSize)
{
	/* CPU Setup (Enable FPU, SSE, etc) */
	CpuBspInit();

	/* Setup output device */
	VideoInit(BootInfo);

	/* Print MollenOS Header */
	printf("MollenOS Operating System - Platform: %s - Version %i.%i.%i\n", 
		ARCHITECTURE_NAME, REVISION_MAJOR, REVISION_MINOR, REVISION_BUILD);
	printf("Written by Philip Meulengracht, Copyright 2011-2014, All Rights Reserved.\n");
	printf("Bootloader - %s\n", (char*)BootInfo->BootLoaderName);
	printf("VC Build %s - %s\n\n", BUILD_DATE, BUILD_TIME);

	/* Setup base components */
	printf("  - Setting up base components\n");
	GdtInit();
	IdtInit();
	ExceptionsInit();
	InterruptInit();

	/* Memory setup! */
	printf("  - Setting up memory systems\n");
	printf("    * Physical Memory Manager...\n");
	MmPhyiscalInit(BootInfo, KernelSize);
	printf("    * Virtual Memory Manager...\n");
	MmVirtualInit();
	printf("    * Heap Memory Manager...\n");
	HeapInit();

	/* Setup early ACPICA */
	printf("  - Initializing ACPI Systems\n");
	AcpiInitStage1();

	/* Enable the APIC */
	printf("    * APIC Initializing\n");
	ApicBspInit();

	/* Threading */
	printf("  - Threading\n");
	SchedulerInit(0);
	ThreadingInit();

	/* Setup Timers */
	printf("    * Setting up local timer\n");
	ApicTimerInit();

	/* Start out any extra cores */
	printf("  - Booting Cores\n");
	//SmpInit();

	/* Setup Full APICPA */
	AcpiInitStage2();

	/* From this point, we should start seperate threads and
	 * let this thread die out, because initial system setup
	 * is now totally done, and the moment we start another
	 * thread, it will take over as this is the idle thread */

	/* Drivers... Damn drivers.. */
	printf("  - Initializing Drivers...\n");
	ThreadingCreateThread("DriverSetup", DriverManagerInit, NULL, 0);

	/* Done with setup!
	 * This should be called on a new thread */
	//threading_create_thread("SystemSetup", mcore_entry, NULL, 0);
}