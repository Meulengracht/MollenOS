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
* MollenOS x86-32 Global Descriptor Table
*/

#include <gdt.h>
#include <string.h>

/* We have no memory allocation system 
 * in place yet, so uhm, allocate in place */
gdt_t gdt_ptr;
gdt_entry_t gdt_descriptors[X86_GDT_MAX_DESCRIPTORS];
tss_entry_t tss_descriptors[X86_GDT_MAX_TSS];

/* This keeps track of how many descriptors are installed 
 * its just easier this way. */
int gdt_index = 0;

void gdt_init(void)
{
	/* Setup the base GDT */
	gdt_ptr.limit = (sizeof(gdt_entry_t) * X86_GDT_MAX_DESCRIPTORS) - 1;
	gdt_ptr.base = (uint32_t)&gdt_descriptors[0];
	gdt_index = 0;

	/* Install Mandatory Descriptors */

	/* NULL Descriptor */
	gdt_install_descriptor(0, 0, 0, 0);

	/* Kernel Code & Data */
	gdt_install_descriptor(0, 0xFFFFFFFF, X86_GDT_KERNEL_CODE, 0xCF); 
	gdt_install_descriptor(0, 0xFFFFFFFF, X86_GDT_KERNEL_DATA, 0xCF);

	/* Userland Code & Data */
	gdt_install_descriptor(0, 0xFFFFFFFF, X86_GDT_USER_CODE, 0xCF);
	gdt_install_descriptor(0, 0xFFFFFFFF, X86_GDT_USER_DATA, 0xCF);

	/* Null task registers */
	memset(&tss_descriptors, 0, sizeof(tss_descriptors));

	/* Reload GDT */
	gdt_install();

	/* Install first TSS, for the boot core */
	gdt_install_tss();
}

void gdt_install_descriptor(uint32_t base, uint32_t limit, 
	uint8_t access, uint8_t grandularity)
{
	/* Null the entry */
	memset(&gdt_descriptors[gdt_index], 0, sizeof(gdt_entry_t));

	/* Base Address */
	gdt_descriptors[gdt_index].base_lo = (uint16_t)(base & 0xFFFF);
	gdt_descriptors[gdt_index].base_mid = (uint8_t)((base >> 16) & 0xFF);
	gdt_descriptors[gdt_index].base_high = (uint8_t)((base >> 24) & 0xFF);
	
	/* Limits */
	gdt_descriptors[gdt_index].limit_lo = (uint16_t)(limit & 0xFFFF);
	gdt_descriptors[gdt_index].flags = (uint8_t)((limit >> 16) & 0x0F);
	
	/* Granularity */
	gdt_descriptors[gdt_index].flags |= (grandularity & 0xF0);
	
	/* Access flags */
	gdt_descriptors[gdt_index].access = access;

	/* Increase index */
	gdt_index++;
}

void gdt_install_tss(void)
{
	/* Get current CPU */
	uint32_t cpu = 0;
	uint32_t tss_index = gdt_index;

	/* Get appropriate TSS */
	uintptr_t base = (uintptr_t)&tss_descriptors[cpu];
	uintptr_t limit = base + sizeof(tss_entry_t);

	/* Setup TSS */
	tss_descriptors[cpu].ss0 = 0x10;
	tss_descriptors[cpu].esp0 = 0;
	
	/* Zero out the descriptors */
	tss_descriptors[cpu].cs = 0x0b;
	tss_descriptors[cpu].ss = 0x13;
	tss_descriptors[cpu].ds = 0x13;
	tss_descriptors[cpu].es = 0x13;
	tss_descriptors[cpu].fs = 0x13;
	tss_descriptors[cpu].gs = 0x13;
	tss_descriptors[cpu].io_map = 0xFFFF;

	/* Install TSS */
	gdt_install_descriptor(base, limit, X86_GDT_TSS_ENTRY, 0x00);

	/* Install into system */
	tss_install(tss_index);
}

void gdt_update_tss(uint32_t cpu, uint32_t stack)
{
	/* Update Interrupt Stack */
	tss_descriptors[cpu].esp0 = stack;
}