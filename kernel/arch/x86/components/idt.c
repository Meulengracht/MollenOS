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

#include <idt.h>
#include <string.h>

/* We have no memory allocation system 
 * in place yet, so uhm, allocate in place */
idt_t idt_ptr;
idt_entry_t idt_descriptors[X86_IDT_DESCRIPTORS];

void idt_init(void)
{
	/* Setup the IDT */
	idt_ptr.limit = (sizeof(idt_entry_t) * X86_IDT_DESCRIPTORS) - 1;
	idt_ptr.base = (uint32_t)&idt_descriptors[0];

	/* Null out entries */
	memset(&idt_descriptors[0], 0, sizeof(idt_descriptors));

	/* Reload GDT */
	idt_install();
}

void idt_install_descriptor(uint32_t int_num, uint32_t base,
	uint16_t selector, uint8_t flags)
{
	/* Set Address */
	idt_descriptors[int_num].base_lo = (base & 0xFFFF);
	idt_descriptors[int_num].base_high = ((base >> 16) & 0xFFFF);

	/* Selector */
	idt_descriptors[int_num].selector = selector;

	/* Zero */
	idt_descriptors[int_num].zero = 0;

	/* Flags! */
	idt_descriptors[int_num].info = flags;
}