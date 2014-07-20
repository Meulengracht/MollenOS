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
#include <pci.h>
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
irq_entry_t irq_table[X86_IDT_DESCRIPTORS][X86_MAX_HANDLERS_PER_INTERRUPT];

/* Install a interrupt handler */
void _interrupt_install(uint32_t irq, uint32_t idt_entry, uint64_t apic_entry, irq_handler_t callback, void *args)
{
	/* Determine Correct Irq */
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
			/* Do we need to redirect? 
			 * TODO Extract Trigger mode & Polarity if redirect is
			 * avail */
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

	/* Install into table */
	interrupt_install_soft(idt_entry, callback, args);

	/* Do ACPI */
	/* TODO!!!! FIND CORRECT APIC IO if multiple */

	/* i_irq is the initial irq */
	apic_write_entry_io(0, (0x10 + (i_irq * 2)), apic_entry);
}

/* Install a normal, lowest priority interrupt */
void interrupt_install(uint32_t irq, uint32_t idt_entry, irq_handler_t callback, void *args)
{
	uint64_t apic_flags = 0;

	apic_flags = 0xFF00000000000000;	/* Target all groups */
	apic_flags |= 0x100;				/* Lowest Priority */
	apic_flags |= 0x800;				/* Logical Destination Mode */

	/* We have one ACPI Special Case */
	if (irq == AcpiGbl_FADT.SciInterrupt)
	{
		apic_flags |= 0x2000;			/* Active Low */
		apic_flags |= 0x8000;			/* Level Sensitive */
	}

	apic_flags |= idt_entry;			/* Interrupt Vector */

	_interrupt_install(irq, idt_entry, apic_flags, callback, args);
}

/* Install a broadcast, global interupt */
void interrupt_install_broadcast(uint32_t irq, uint32_t idt_entry, irq_handler_t callback, void *args)
{
	uint64_t apic_flags = 0;

	apic_flags = 0xFF00000000000000;	/* Target all groups */
	apic_flags |= 0x800;				/* Logical Destination Mode */
	apic_flags |= idt_entry;			/* Interrupt Vector */

	_interrupt_install(irq, idt_entry, apic_flags, callback, args);
}

/* Install a pci interrupt */
void interrupt_install_pci(pci_driver_t *device, irq_handler_t callback, void *args)
{
	uint64_t apic_flags = 0;
	int result;
	uint8_t trigger_mode = 0, polarity = 0, shareable = 0;
	uint32_t io_entry = 0;
	uint32_t idt_entry = 0x20;
	uint32_t pin;

	/* Get Interrupt Information */
	result = pci_device_get_irq(device->bus, device->device, device->header->interrupt_pin,
		&trigger_mode, &polarity, &shareable);

	/* If no routing exists use the interrupt_line */
	if (result == -1)
	{
		io_entry = device->header->interrupt_line;
		idt_entry += device->header->interrupt_line;

		apic_flags = 0xFF00000000000000;	/* Target all groups */
		apic_flags |= 0x100;				/* Lowest Priority */
		apic_flags |= 0x800;				/* Logical Destination Mode */
	}
	else
	{
		io_entry = (uint32_t)result;
		pin = device->header->interrupt_pin;

		/* Setup APIC flags */
		apic_flags = 0xFF00000000000000;	/* Target all groups */
		apic_flags |= 0x100;				/* Lowest Priority */
		apic_flags |= 0x800;				/* Logical Destination Mode */
		apic_flags |= (polarity << 13);		/* Set Polarity */
		apic_flags |= (trigger_mode << 15);	/* Set Trigger Mode */

		/* Sanity */
		if (pin == 4)
			pin--;

		switch (pin)
		{
		case 0:
		{
			idt_entry = INTERRUPT_PCI_PIN_0;
		} break;
		case 1:
		{
			idt_entry = INTERRUPT_PCI_PIN_1;
		} break;
		case 2:
		{
			idt_entry = INTERRUPT_PCI_PIN_2;
		} break;
		case 3:
		{
			idt_entry = INTERRUPT_PCI_PIN_3;
		} break;

		default:
			break;
		}
	}
	
	/* Set IDT Vector */
	apic_flags |= idt_entry;

	_interrupt_install(io_entry, idt_entry, apic_flags, callback, args);
}

/* Install only the interrupt handler, 
 *  this should be used for software interrupts */
void interrupt_install_soft(uint32_t idt_entry, irq_handler_t callback, void *args)
{
	/* Install into table */
	int i;
	int found = 0;

	/* Find a free interrupt */
	for (i = 0; i < X86_MAX_HANDLERS_PER_INTERRUPT; i++)
	{
		if (irq_table[idt_entry][i].installed)
			continue;
	
		/* Install it */
		irq_table[idt_entry][i].function = callback;
		irq_table[idt_entry][i].data = args;
		irq_table[idt_entry][i].installed = 1;
		found = 1;
		break;
	}

	/* Sanity */
	assert(found != 0);
}

/* The common entry point for interrupts */
void interrupt_entry(registers_t *regs)
{
	/* Determine Irq */
	int i;
	int calls = 0;
	uint32_t irq = regs->irq + 0x20;

	/* Get handler(s) */
	for (i = 0; i < X86_MAX_HANDLERS_PER_INTERRUPT; i++)
	{
		if (irq_table[irq][i].installed)
		{
			/* If no args are specified we give access
			* to registers */
			if (irq_table[irq][i].data == NULL)
				irq_table[irq][i].function((void*)regs);
			else
				irq_table[irq][i].function(irq_table[irq][i].data);

			calls++;
		}
	}
	
	/* Sanity */
	if (calls == 0)
		printf("Unhandled interrupt vector %u\n", irq);

	/* Send EOI */
	apic_send_eoi();
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
	int i, j;

	/* Null out interrupt table */
	memset((void*)&irq_table, 0, sizeof(irq_table));

	/* Setup Stuff */
	for (i = 0; i < X86_IDT_DESCRIPTORS; i++)
	{
		for (j = 4; j < X86_MAX_HANDLERS_PER_INTERRUPT; j++)
		{
			/* Mark reserved interrupts */
			if (i < 0x20)
				irq_table[i][j].installed = 1;
			else
				irq_table[i][j].installed = 0;
		}
	}

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
	idt_install_descriptor(51, (uint32_t)&irq_stringify(51), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(52, (uint32_t)&irq_stringify(52), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(53, (uint32_t)&irq_stringify(53), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(54, (uint32_t)&irq_stringify(54), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(55, (uint32_t)&irq_stringify(55), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(56, (uint32_t)&irq_stringify(56), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(57, (uint32_t)&irq_stringify(57), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(58, (uint32_t)&irq_stringify(58), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(59, (uint32_t)&irq_stringify(59), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(60, (uint32_t)&irq_stringify(60), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(61, (uint32_t)&irq_stringify(61), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(62, (uint32_t)&irq_stringify(62), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(63, (uint32_t)&irq_stringify(63), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(64, (uint32_t)&irq_stringify(64), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(65, (uint32_t)&irq_stringify(65), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(66, (uint32_t)&irq_stringify(66), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(67, (uint32_t)&irq_stringify(67), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(68, (uint32_t)&irq_stringify(68), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(69, (uint32_t)&irq_stringify(69), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(70, (uint32_t)&irq_stringify(70), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(71, (uint32_t)&irq_stringify(71), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(72, (uint32_t)&irq_stringify(72), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(73, (uint32_t)&irq_stringify(73), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(74, (uint32_t)&irq_stringify(74), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(75, (uint32_t)&irq_stringify(75), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(76, (uint32_t)&irq_stringify(76), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(77, (uint32_t)&irq_stringify(77), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(78, (uint32_t)&irq_stringify(78), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(79, (uint32_t)&irq_stringify(79), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(80, (uint32_t)&irq_stringify(80), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(81, (uint32_t)&irq_stringify(81), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(82, (uint32_t)&irq_stringify(82), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(83, (uint32_t)&irq_stringify(83), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(84, (uint32_t)&irq_stringify(84), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(85, (uint32_t)&irq_stringify(85), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(86, (uint32_t)&irq_stringify(86), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(87, (uint32_t)&irq_stringify(87), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(88, (uint32_t)&irq_stringify(88), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(89, (uint32_t)&irq_stringify(89), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(90, (uint32_t)&irq_stringify(90), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(91, (uint32_t)&irq_stringify(91), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(92, (uint32_t)&irq_stringify(92), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(93, (uint32_t)&irq_stringify(93), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(94, (uint32_t)&irq_stringify(94), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(95, (uint32_t)&irq_stringify(95), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(96, (uint32_t)&irq_stringify(96), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(97, (uint32_t)&irq_stringify(97), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(98, (uint32_t)&irq_stringify(98), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(99, (uint32_t)&irq_stringify(99), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(100, (uint32_t)&irq_stringify(100), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(101, (uint32_t)&irq_stringify(101), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(102, (uint32_t)&irq_stringify(102), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(103, (uint32_t)&irq_stringify(103), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(104, (uint32_t)&irq_stringify(104), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(105, (uint32_t)&irq_stringify(105), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(106, (uint32_t)&irq_stringify(106), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(107, (uint32_t)&irq_stringify(107), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(108, (uint32_t)&irq_stringify(108), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(109, (uint32_t)&irq_stringify(109), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(110, (uint32_t)&irq_stringify(110), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(111, (uint32_t)&irq_stringify(111), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(112, (uint32_t)&irq_stringify(112), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(113, (uint32_t)&irq_stringify(113), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(114, (uint32_t)&irq_stringify(114), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(115, (uint32_t)&irq_stringify(115), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(116, (uint32_t)&irq_stringify(116), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(117, (uint32_t)&irq_stringify(117), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(118, (uint32_t)&irq_stringify(118), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(119, (uint32_t)&irq_stringify(119), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(120, (uint32_t)&irq_stringify(120), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(121, (uint32_t)&irq_stringify(121), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(122, (uint32_t)&irq_stringify(122), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(123, (uint32_t)&irq_stringify(123), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(124, (uint32_t)&irq_stringify(124), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(125, (uint32_t)&irq_stringify(125), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(126, (uint32_t)&irq_stringify(126), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);

	/* Spurious */
	idt_install_descriptor(127, (uint32_t)&irq_stringify(127), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);

	/* Syscalls */
	idt_install_descriptor(128, (uint32_t)&irq_stringify(128), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_TRAP_GATE32);

	/* Yield */
	idt_install_descriptor(129, (uint32_t)&irq_stringify(129), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);

	/* Next Batch */
	idt_install_descriptor(130, (uint32_t)&irq_stringify(130), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(131, (uint32_t)&irq_stringify(131), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(132, (uint32_t)&irq_stringify(132), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(133, (uint32_t)&irq_stringify(133), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(134, (uint32_t)&irq_stringify(134), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(135, (uint32_t)&irq_stringify(135), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(136, (uint32_t)&irq_stringify(136), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(137, (uint32_t)&irq_stringify(137), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(138, (uint32_t)&irq_stringify(138), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(139, (uint32_t)&irq_stringify(139), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(140, (uint32_t)&irq_stringify(140), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(141, (uint32_t)&irq_stringify(141), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(142, (uint32_t)&irq_stringify(142), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(143, (uint32_t)&irq_stringify(143), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(144, (uint32_t)&irq_stringify(144), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(145, (uint32_t)&irq_stringify(145), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(146, (uint32_t)&irq_stringify(146), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(147, (uint32_t)&irq_stringify(147), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(148, (uint32_t)&irq_stringify(148), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(149, (uint32_t)&irq_stringify(149), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(150, (uint32_t)&irq_stringify(150), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(151, (uint32_t)&irq_stringify(151), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(152, (uint32_t)&irq_stringify(152), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(153, (uint32_t)&irq_stringify(153), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(154, (uint32_t)&irq_stringify(154), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(155, (uint32_t)&irq_stringify(155), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(156, (uint32_t)&irq_stringify(156), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(157, (uint32_t)&irq_stringify(157), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(158, (uint32_t)&irq_stringify(158), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(159, (uint32_t)&irq_stringify(159), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(160, (uint32_t)&irq_stringify(160), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(161, (uint32_t)&irq_stringify(161), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(162, (uint32_t)&irq_stringify(162), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(163, (uint32_t)&irq_stringify(163), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(164, (uint32_t)&irq_stringify(164), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(165, (uint32_t)&irq_stringify(165), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(166, (uint32_t)&irq_stringify(166), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(167, (uint32_t)&irq_stringify(167), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(168, (uint32_t)&irq_stringify(168), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(169, (uint32_t)&irq_stringify(169), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(170, (uint32_t)&irq_stringify(170), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(171, (uint32_t)&irq_stringify(171), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(172, (uint32_t)&irq_stringify(172), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(173, (uint32_t)&irq_stringify(173), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(174, (uint32_t)&irq_stringify(174), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(175, (uint32_t)&irq_stringify(175), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(176, (uint32_t)&irq_stringify(176), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(177, (uint32_t)&irq_stringify(177), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(178, (uint32_t)&irq_stringify(178), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(179, (uint32_t)&irq_stringify(179), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(180, (uint32_t)&irq_stringify(180), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(181, (uint32_t)&irq_stringify(181), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(182, (uint32_t)&irq_stringify(182), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(183, (uint32_t)&irq_stringify(183), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(184, (uint32_t)&irq_stringify(184), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(185, (uint32_t)&irq_stringify(185), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(186, (uint32_t)&irq_stringify(186), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(187, (uint32_t)&irq_stringify(187), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(188, (uint32_t)&irq_stringify(188), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(189, (uint32_t)&irq_stringify(189), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(190, (uint32_t)&irq_stringify(190), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(191, (uint32_t)&irq_stringify(191), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(192, (uint32_t)&irq_stringify(192), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(193, (uint32_t)&irq_stringify(193), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(194, (uint32_t)&irq_stringify(194), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(195, (uint32_t)&irq_stringify(195), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(196, (uint32_t)&irq_stringify(196), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(197, (uint32_t)&irq_stringify(197), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(198, (uint32_t)&irq_stringify(198), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(199, (uint32_t)&irq_stringify(199), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(200, (uint32_t)&irq_stringify(200), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(201, (uint32_t)&irq_stringify(201), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(202, (uint32_t)&irq_stringify(202), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(203, (uint32_t)&irq_stringify(203), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(204, (uint32_t)&irq_stringify(204), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(205, (uint32_t)&irq_stringify(205), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(206, (uint32_t)&irq_stringify(206), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(207, (uint32_t)&irq_stringify(207), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(208, (uint32_t)&irq_stringify(208), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(209, (uint32_t)&irq_stringify(209), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(210, (uint32_t)&irq_stringify(210), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(211, (uint32_t)&irq_stringify(211), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(212, (uint32_t)&irq_stringify(212), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(213, (uint32_t)&irq_stringify(213), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(214, (uint32_t)&irq_stringify(214), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(215, (uint32_t)&irq_stringify(215), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(216, (uint32_t)&irq_stringify(216), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(217, (uint32_t)&irq_stringify(217), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(218, (uint32_t)&irq_stringify(218), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(219, (uint32_t)&irq_stringify(219), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(220, (uint32_t)&irq_stringify(220), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);			/* PCI Interrupt 0 */
	idt_install_descriptor(221, (uint32_t)&irq_stringify(221), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(222, (uint32_t)&irq_stringify(222), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(223, (uint32_t)&irq_stringify(223), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(224, (uint32_t)&irq_stringify(224), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);			/* PCI Interrupt 1 */
	idt_install_descriptor(225, (uint32_t)&irq_stringify(225), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(226, (uint32_t)&irq_stringify(226), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(227, (uint32_t)&irq_stringify(227), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(228, (uint32_t)&irq_stringify(228), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);			/* PCI Interrupt 2 */
	idt_install_descriptor(229, (uint32_t)&irq_stringify(229), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(230, (uint32_t)&irq_stringify(230), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(231, (uint32_t)&irq_stringify(231), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);

	/* Hardware Ints */
	idt_install_descriptor(232, (uint32_t)&irq_stringify(232), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);			/* PCI Interrupt 3 */
	idt_install_descriptor(233, (uint32_t)&irq_stringify(233), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(234, (uint32_t)&irq_stringify(234), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(235, (uint32_t)&irq_stringify(235), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(236, (uint32_t)&irq_stringify(236), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(237, (uint32_t)&irq_stringify(237), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(238, (uint32_t)&irq_stringify(238), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(239, (uint32_t)&irq_stringify(239), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(240, (uint32_t)&irq_stringify(240), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(241, (uint32_t)&irq_stringify(241), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(242, (uint32_t)&irq_stringify(242), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(243, (uint32_t)&irq_stringify(243), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(244, (uint32_t)&irq_stringify(244), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(245, (uint32_t)&irq_stringify(245), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(246, (uint32_t)&irq_stringify(246), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(247, (uint32_t)&irq_stringify(247), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(248, (uint32_t)&irq_stringify(248), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(249, (uint32_t)&irq_stringify(249), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(250, (uint32_t)&irq_stringify(250), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(251, (uint32_t)&irq_stringify(251), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(252, (uint32_t)&irq_stringify(252), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(253, (uint32_t)&irq_stringify(253), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(254, (uint32_t)&irq_stringify(254), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(255, (uint32_t)&irq_stringify(255), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
}