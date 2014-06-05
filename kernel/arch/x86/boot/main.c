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
#include <arch.h>
#include <gdt.h>
#include <idt.h>
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
	  
	/* Memory setup! */
	printf("  - Setting up memory systems\n");
	printf("    * Physical Memory Manager...\n");
	physmem_init(bootinfo, kernel_size);
	printf("    * Virtual Memory Manager...\n");
	virtmem_init();
	printf("    * Heap Memory Manager...\n");
	heap_init();

	/* Setup early ACPICA */

	/* Install Basic Threading */

	/* Setup Full APICPA */

	/* Startup AP cores */

	/* Done with setup! 
	 * This should be called on a new thread */
	mcore_entry(NULL);
}