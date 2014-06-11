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
#include <idt.h>
#include <gdt.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <list.h>

/* Internal Defines */
#define EFLAGS_INTERRUPT_FLAG (1 << 9)

/* Externs */
extern void __cli(void);
extern void __sti(void);
extern uint32_t __getflags(void);
extern list_t *acpi_nodes;

/* Globals */
irq_entry_t irq_table[X86_IDT_DESCRIPTORS];

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

/* Initiates Interrupt Handlers */
void interrupt_init(void)
{
	/* Null out interrupt table */
	memset((void*)&irq_table, 0, sizeof(irq_table));

	/* First 32 descriptors are reserved */
}

/* Install a interrupt handler */
void interrupt_install(uint32_t irq, irq_handler_t callback, void *args)
{
	/* Determine Correct Irq */
	uint32_t c_irq = 0x20;
	uint32_t i_irq = irq;

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
}

void interrupt_entry(registers_t *regs)
{
	/* Determine Irq */
	uint32_t irq = regs->irq + 0x20;

	/* Get handler */
	if (irq_table[irq].function != NULL)
	{
		irq_table[irq].function(irq_table[irq].data);
	}
	else
	{
		printf("Unhandled interrupt vector %u\n", irq);
	}
}