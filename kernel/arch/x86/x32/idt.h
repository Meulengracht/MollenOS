/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * x86-32 Descriptor Table
 * - Interrupt Descriptor Table
 */

#ifndef __X86_IDT_H__
#define __X86_IDT_H__

#include <os/osdefs.h>

/* IDT Entry Types
 * These are the possible interrupt gate types, we have:
 * Interrupt-Gates 16/32 Bit - They automatically disable interrupts
 * Trap-Gates 16/32 Bit - They don't disable interrupts 
 * Task-Gates 32 Bit - Hardware Task Switching */
#define IDT_INTERRUPT_GATE16		0x6
#define IDT_INTERRUPT_GATE32		0xE
#define IDT_TRAP_GATE16				0x7
#define IDT_TRAP_GATE32				0xF
#define IDT_TASK_GATE32				0x5

/* IDT Priveliege Types
 * This specifies which ring can use/be interrupt by
 * the idt-entry, we usually specify RING3 */
#define IDT_RING0					0x00
#define IDT_RING1					0x20
#define IDT_RING2					0x40
#define IDT_RING3					0x60

/* IDT Flags
 * Specifies any special attributes about the IDT entry
 * Present must be set for all valid idt-entries */
#define IDT_STORAGE_SEGMENT			0x10
#define IDT_PRESENT					0x80

/* The IDT base structure, this is what the hardware
 * will poin to, that describes the memory range where
 * all the idt-descriptors reside */
PACKED_TYPESTRUCT(IdtObject, {
	uint16_t			Limit;
	uint32_t			Base;
});

/* The IDT descriptor structure, this is the actual entry
 * in the idt table, and keeps information about the 
 * interrupt structure. */
PACKED_TYPESTRUCT(IdtEntry, {
	uint16_t			BaseLow;	/* Base 0:15 */
	uint16_t			Selector;	/* Selector */
	uint8_t				Zero;		/* Reserved */

	/* IDT Entry Flags
	 * Bits 0-3: Descriptor Entry Type
	 * Bits   4: Storage Segment
	 * Bits 5-6: Priveliege Level
	 * Bits   7: Present */
	uint8_t				Flags;
	uint16_t			BaseHigh;	/* Base 16:31 */
});

/* Initialize the idt table with the 256 default
 * descriptors for entering shared interrupt handlers
 * and shared exception handlers */
__EXTERN void IdtInitialize(void);
/* This installs the current idt-object in the
 * idt register for the calling cpu, use to setup idt */
__EXTERN void IdtInstall(void);

/* Extern to the syscall-handler */
__EXTERN void syscall_entry(void);

#endif // !__X86_IDT_H__
