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

#ifndef _x86_IDT_H_
#define _x86_IDT_H_

/* IDT Includes */
#include <crtdefs.h>
#include <stdint.h>

/* IDT Definitions */
#define X86_IDT_DESCRIPTORS			256

/* Types */
#define X86_IDT_INTERRUPT_GATE16	0x6
#define X86_IDT_TRAP_GATE16			0x7
#define X86_IDT_TASK_GATE32			0x5		/* Task gates are for switching hardware tasks, not used */
#define X86_IDT_INTERRUPT_GATE32	0xE		/* Interrupt gates automatically disable interrupts */
#define X86_IDT_TRAP_GATE32			0xF		/* Trap gates do not */

/* Priveligies */
#define X86_IDT_RING0				0x00
#define X86_IDT_RING1				0x20
#define X86_IDT_RING2				0x40
#define X86_IDT_RING3				0x60	/* Always set this if interrupt can occur from userland */

/* Flags */
#define X86_IDT_STORAGE_SEGMENT		0x10	/* Should not be set for interrupts gates */
#define X86_IDT_PRESENT				0x80	/* Always set this! */

/* IDT Structures */
#pragma pack(push, 1)
typedef struct idt
{
	/* Size */
	uint16_t limit;

	/* Pointer to table */
	uint32_t base;
} idt_t;
#pragma pack(pop)

/* IDT Entry */
#pragma pack(push, 1)
typedef struct idt_entry
{
	/* Base 0:15 */
	uint16_t base_lo;

	/* Selector */
	uint16_t selector;

	/* Zero */
	uint8_t zero;

	/* Descriptor Type 0:3 
	 * Storage Segment 4 
	 * DPL 5:6
	 * Present 7 */
	uint8_t info;

	/* Base 16:31 */
	uint16_t base_high;

} idt_entry_t;
#pragma pack(pop)

/* IDT Prototypes */
_CRT_EXTERN void idt_init(void);
_CRT_EXTERN void idt_install_descriptor(uint32_t int_num, uint32_t base, 
										uint16_t selector, uint8_t flags);

/* Should be called by AP cores */
_CRT_EXTERN void idt_install(void);

#endif // !_x86_GDT_H_
