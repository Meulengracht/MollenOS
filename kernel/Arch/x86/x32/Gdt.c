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

#include <Gdt.h>
#include <string.h>

/* We have no memory allocation system 
 * in place yet, so uhm, allocate in place */
Gdt_t Gdtptr;
GdtEntry_t GdtDescriptors[X86_GDT_MAX_DESCRIPTORS];
TssEntry_t TssDescriptors[X86_GDT_MAX_TSS];

/* This keeps track of how many descriptors are installed 
 * its just easier this way. */
volatile int32_t GblGdtIndex = 0;

void GdtInit(void)
{
	/* Setup the base GDT */
	Gdtptr.Limit = (sizeof(GdtEntry_t) * X86_GDT_MAX_DESCRIPTORS) - 1;
	Gdtptr.Base = (uint32_t)&GdtDescriptors[0];
	GblGdtIndex = 0;

	/* Install Mandatory Descriptors */

	/* NULL Descriptor */
	GdtInstallDescriptor(0, 0, 0, 0);

	/* Kernel Code & Data */
	GdtInstallDescriptor(0, 0xFFFFFFFF, X86_GDT_KERNEL_CODE, 0xCF);
	GdtInstallDescriptor(0, 0xFFFFFFFF, X86_GDT_KERNEL_DATA, 0xCF);

	/* Userland Code & Data */
	GdtInstallDescriptor(0, 0xFFFFFFFF, X86_GDT_USER_CODE, 0xCF);
	GdtInstallDescriptor(0, 0xFFFFFFFF, X86_GDT_USER_DATA, 0xCF);

	/* Null task registers */
	memset(&TssDescriptors, 0, sizeof(TssDescriptors));

	/* Reload GDT */
	GdtInstall();

	/* Install first TSS, for the boot core */
	GdtInstallTss();
}

void GdtInstallDescriptor(uint32_t Base, uint32_t Limit,
	uint8_t Access, uint8_t Grandularity)
{
	/* Null the entry */
	memset(&GdtDescriptors[GblGdtIndex], 0, sizeof(GdtEntry_t));

	/* Base Address */
	GdtDescriptors[GblGdtIndex].BaseLow = (uint16_t)(Base & 0xFFFF);
	GdtDescriptors[GblGdtIndex].BaseMid = (uint8_t)((Base >> 16) & 0xFF);
	GdtDescriptors[GblGdtIndex].BaseHigh = (uint8_t)((Base >> 24) & 0xFF);
	
	/* Limits */
	GdtDescriptors[GblGdtIndex].LimitLow = (uint16_t)(Limit & 0xFFFF);
	GdtDescriptors[GblGdtIndex].Flags = (uint8_t)((Limit >> 16) & 0x0F);
	
	/* Granularity */
	GdtDescriptors[GblGdtIndex].Flags |= (Grandularity & 0xF0);
	
	/* Access flags */
	GdtDescriptors[GblGdtIndex].Access = Access;

	/* Increase index */
	GblGdtIndex++;
}

void GdtInstallTss(void)
{
	/* Get current CPU */
	uint32_t cpu = 0;
	uint32_t tss_index = GblGdtIndex;

	/* Get appropriate TSS */
	uintptr_t base = (uintptr_t)&TssDescriptors[cpu];
	uintptr_t limit = base + sizeof(TssEntry_t);

	/* Setup TSS */
	TssDescriptors[cpu].Ss0 = 0x10;
	TssDescriptors[cpu].Esp0 = 0;
	
	/* Zero out the descriptors */
	TssDescriptors[cpu].Cs = 0x0b;
	TssDescriptors[cpu].Ss = 0x13;
	TssDescriptors[cpu].Ds = 0x13;
	TssDescriptors[cpu].Es = 0x13;
	TssDescriptors[cpu].Fs = 0x13;
	TssDescriptors[cpu].Gs = 0x13;
	TssDescriptors[cpu].IoMap = 0xFFFF;

	/* Install TSS */
	GdtInstallDescriptor(base, limit, X86_GDT_TSS_ENTRY, 0x00);

	/* Install into system */
	TssInstall(tss_index);
}

void TssUpdateStack(uint32_t Cpu, uint32_t Stack)
{
	/* Update Interrupt Stack */
	TssDescriptors[Cpu].Esp0 = Stack;
}