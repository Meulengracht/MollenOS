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
* MollenOS x86-32 Interrupt Handlers & Init
*/

#include <assert.h>
#include <arch.h>
#include <acpi.h>
#include <lapic.h>
#include <idt.h>
#include <gdt.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <list.h>

/* Internal Defines */
#define EFLAGS_INTERRUPT_FLAG (1 << 9)
#define irq_stringify(irq) irq_handler##irq

/* Externs */
extern void __cli(void);
extern void __sti(void);
extern void __hlt(void);
extern uint32_t __getflags(void);
extern list_t *acpi_nodes;

/* Globals */
irq_entry_t irq_table[X86_IDT_DESCRIPTORS];

/* Install a interrupt handler */
void interrupt_install(uint32_t irq, irq_handler_t callback, void *args)
{
	/* Determine Correct Irq */
	uint32_t c_irq = 0x20;
	uint32_t i_irq = irq;
	uint64_t apic_flags = 0;

	/* Sanity */
	assert(irq < X86_IDT_DESCRIPTORS);

	/* Uh, check for ACPI redirection */
	if (acpi_nodes != NULL)
	{
		ACPI_MADT_INTERRUPT_OVERRIDE *io_redirect = list_get_data_by_id(acpi_nodes, ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, 0);
		int n = 1;

		while (io_redirect != NULL)
		{
			/* Do we need to redirect? */
			if (io_redirect->SourceIrq == irq)
			{
				i_irq = io_redirect->GlobalIrq;
				break;
			}

			/* Get next io redirection */
			io_redirect = list_get_data_by_id(acpi_nodes, ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, n);
			n++;
		}
	}
	
	c_irq += i_irq;

	/* Install into table */
	irq_table[c_irq].function = callback;
	irq_table[c_irq].data = args;

	/* Do ACPI */
	/* TODO!!!! Check that i_irq is LOWER than max redirection entries */

	/* Update APIC */
	/* Setup flags */
	apic_flags |= 0x0F;	/* Target all groups */
	apic_flags <<= 56;
	apic_flags |= 0x100; /* Lowest Priority */
	apic_flags |= 0x800; /* Logical Destination Mode */

	/* i_irq is the initial irq */
	apic_write_entry_io(0, (0x10 + (i_irq * 2)), (apic_flags | (0x20 + i_irq)));
}

/* Install only the interrupt handler, 
 *  this should be used for software interrupts */
void interrupt_install_soft(uint32_t idt_entry, irq_handler_t callback, void *args)
{
	/* Install into table */
	irq_table[idt_entry].function = callback;
	irq_table[idt_entry].data = args;
}

/* The common entry point for interrupts */
void interrupt_entry(registers_t *regs)
{
	/* Determine Irq */
	uint32_t irq = regs->irq + 0x20;

	/* Get handler */
	if (irq_table[irq].function != NULL)
	{
		/* If no args are specified we give access 
		 * to registers */
		if (irq_table[irq].data == NULL)
			irq_table[irq].function(regs);
		else
			irq_table[irq].function(irq_table[irq].data);
	}
	else
	{
		printf("Unhandled interrupt vector %u\n", irq);
	}
}

/* Disables interrupts and returns
* the state before disabling */
interrupt_status_t interrupt_disable(void)
{
	interrupt_status_t cur_state = interrupt_get_state();
	__cli();
	return cur_state;
}

/* Enables interrupts and returns
* the state before enabling */
interrupt_status_t interrupt_enable(void)
{
	interrupt_status_t cur_state = interrupt_get_state();
	__sti();
	return cur_state;
}

/* Restores the state to the given
* state */
interrupt_status_t interrupt_set_state(interrupt_status_t state)
{
	if (state != 0)
		return interrupt_enable();
	else
		return interrupt_disable();

}

/* Gets the current interrupt state */
interrupt_status_t interrupt_get_state(void)
{
	interrupt_status_t status = (interrupt_status_t)__getflags();
	if (status & EFLAGS_INTERRUPT_FLAG)
		return 1;
	else
		return 0;
}

/* Returns whether or not interrupts are
* disabled */
interrupt_status_t interrupt_is_disabled(void)
{
	return !interrupt_get_state();
}

/* Idles using HALT */
void idle(void)
{
	__hlt();
}

/* Initiates Interrupt Handlers */
void interrupt_init(void)
{
	/* Null out interrupt table */
	memset((void*)&irq_table, 0, sizeof(irq_table));

	/* Install Irqs */
	idt_install_descriptor(32, (uint32_t)&irq_stringify(32), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(33, (uint32_t)&irq_stringify(33), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(34, (uint32_t)&irq_stringify(34), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(35, (uint32_t)&irq_stringify(35), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(36, (uint32_t)&irq_stringify(36), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(37, (uint32_t)&irq_stringify(37), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(38, (uint32_t)&irq_stringify(38), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(39, (uint32_t)&irq_stringify(39), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(40, (uint32_t)&irq_stringify(40), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(41, (uint32_t)&irq_stringify(41), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(42, (uint32_t)&irq_stringify(42), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(43, (uint32_t)&irq_stringify(43), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(44, (uint32_t)&irq_stringify(44), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(45, (uint32_t)&irq_stringify(45), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(46, (uint32_t)&irq_stringify(46), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(47, (uint32_t)&irq_stringify(47), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(48, (uint32_t)&irq_stringify(48), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(49, (uint32_t)&irq_stringify(49), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(50, (uint32_t)&irq_stringify(50), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);

	/* Spurious */
	idt_install_descriptor(127, (uint32_t)&irq_stringify(127), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);

	/* Syscalls */
	idt_install_descriptor(128, (uint32_t)&irq_stringify(128), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_TRAP_GATE32);

	/* Yield */
	idt_install_descriptor(129, (uint32_t)&irq_stringify(129), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
}