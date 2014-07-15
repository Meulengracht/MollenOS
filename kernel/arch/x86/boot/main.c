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
#include <arch.h>
#include <lapic.h>
#include <multiboot.h>
#include <gdt.h>
#include <thread.h>
#include <scheduler.h>
#include <idt.h>
#include <cpu.h>
#include <exceptions.h>

#include <stddef.h>
#include <stdio.h>

/* Extern, this function is declared in the MCore project
 * and all platform libs should enter this function */
extern void mcore_entry(void*);

/* These functions are cross-platform and located in the
 * MCore project. All platform libs should call these 
 * whenever possible. */
extern void heap_init(void);
extern void apic_print_debug_cpu(void);

void init(multiboot_info_t *bootinfo, uint32_t kernel_size)
{
	/* Setup output device */
	video_init(bootinfo);

	/* Print MollenOS Header */
	printf("MollenOS Operating System - Platform: %s - Version %i.%i.%i\n", 
		ARCHITECTURE_NAME, REVISION_MAJOR, REVISION_MINOR, REVISION_BUILD);
	printf("Written by Philip Meulengracht, Copyright 2011-2014, All Rights Reserved.\n");
	printf("Bootloader - %s\n", (char*)bootinfo->BootLoaderName);
	printf("VC Build %s - %s\n\n", BUILD_DATE, BUILD_TIME);

	/* Setup base components */
	printf("  - Setting up base components\n");
	printf("    * Installing GDT...\n");
	gdt_init();
	printf("    * Installing IDT...\n");
	idt_init();
	printf("    * Installing Interrupts...\n");
	exceptions_init();
	interrupt_init();

	/* CPU Setup */
	cpu_boot_init();
	  
	/* Memory setup! */
	printf("  - Setting up memory systems\n");
	printf("    * Physical Memory Manager...\n");
	physmem_init(bootinfo, kernel_size);
	printf("    * Virtual Memory Manager...\n");
	virtmem_init();
	printf("    * Heap Memory Manager...\n");
	heap_init();

	/* Setup early ACPICA */
	printf("  - Initializing ACPI Systems\n");
	acpi_init_stage1();

	/* Enable the APIC */
	printf("    * APIC Initializing\n");
	apic_init();

	/* Threading */
	printf("  - Threading\n");
	scheduler_init();
	threading_init();

	/* Setup Timers */
	printf("    * Setting up local timer\n");
	apic_timer_init();

	/* Setup Full APICPA */
	acpi_init_stage2();

	/* Start out any extra cores */
	printf("  - Booting Cores\n");
	cpu_ap_init();

	/* Drivers... Damn drivers.. */
	printf("  - Enumerating Drivers...\n");
	drivers_init();

	/* Done with setup! 
	 * This should be called on a new thread */
	threading_create_thread("SystemSetup", mcore_entry, NULL, 0);
}