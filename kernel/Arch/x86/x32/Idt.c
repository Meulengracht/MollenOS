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
* MollenOS x86-32 Interrupt Descriptor Table
*/

#include <Idt.h>
#include <string.h>

/* We have no memory allocation system 
 * in place yet, so uhm, allocate in place */
Idt_t Idtptr;
IdtEntry_t IdtDescriptors[X86_IDT_DESCRIPTORS];

void IdtInit(void)
{
	/* Setup the IDT */
	Idtptr.Limit = (sizeof(IdtEntry_t) * X86_IDT_DESCRIPTORS) - 1;
	Idtptr.Base = (uint32_t)&IdtDescriptors[0];

	/* Null out entries */
	memset(&IdtDescriptors[0], 0, sizeof(IdtDescriptors));

	/* Reload GDT */
	IdtInstall();
}

void IdtInstallDescriptor(uint32_t IntNum, uint32_t Base,
	uint16_t Selector, uint8_t Flags)
{
	/* Set Address */
	IdtDescriptors[IntNum].BaseLow = (Base & 0xFFFF);
	IdtDescriptors[IntNum].BaseHigh = ((Base >> 16) & 0xFFFF);

	/* Selector */
	IdtDescriptors[IntNum].Selector = Selector;

	/* Zero */
	IdtDescriptors[IntNum].Zero = 0;

	/* Flags! */
	IdtDescriptors[IntNum].Info = Flags;
}