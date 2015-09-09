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
#include <Arch.h>
#include <Acpi.h>
#include <Pci.h>
#include <LApic.h>
#include <Idt.h>
#include <Gdt.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <List.h>

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
IrqEntry_t IrqTable[X86_IDT_DESCRIPTORS][X86_MAX_HANDLERS_PER_INTERRUPT];
uint32_t IrqIsaTable[X86_NUM_ISA_INTERRUPTS];

/* Parses Intiflags for Polarity */
uint32_t InterruptGetPolarity(uint16_t IntiFlags, uint8_t IrqSource)
{
	/* Returning 1 means LOW, returning 0 means HIGH */
	switch (IntiFlags & ACPI_MADT_POLARITY_MASK)
	{
		case ACPI_MADT_POLARITY_CONFORMS:
		{
			if (IrqSource == AcpiGbl_FADT.SciInterrupt)
				return 1;
			else
				return 0;
		} break;

		/* Active High */
		case ACPI_MADT_POLARITY_ACTIVE_HIGH:
			return 0;
		case ACPI_MADT_POLARITY_ACTIVE_LOW:
			return 1;
	}

	return 0;
}

/* Parses Intiflags for Trigger Mode */
uint32_t InterruptGetTrigger(uint16_t IntiFlags, uint8_t IrqSource)
{
	/* Returning 1 means LEVEL, returning 0 means EDGE */
	switch (IntiFlags & ACPI_MADT_TRIGGER_MASK)
	{
		case ACPI_MADT_TRIGGER_CONFORMS:
		{
			if (IrqSource == AcpiGbl_FADT.SciInterrupt)
				return 1;
			else
				return 0;
		} break;

		/* Active High */
		case ACPI_MADT_TRIGGER_EDGE:
			return 0;
		case ACPI_MADT_TRIGGER_LEVEL:
			return 1;
	}

	return 0;
}

/* Install a interrupt handler */
void InterruptInstallBase(uint32_t Irq, uint32_t IdtEntry, uint64_t ApicEntry, IrqHandler_t Callback, void *Args)
{
	/* Determine Correct Irq */
	uint32_t i_irq = Irq;
	uint64_t i_apic = ApicEntry;
	uint64_t CheckApicEntry = 0;
	uint32_t upper = 0;
	uint32_t lower = 0;
	IoApic_t *IoApic;

	/* Sanity */
	assert(Irq < X86_IDT_DESCRIPTORS);

	/* Uh, check for ACPI redirection */
	if (acpi_nodes != NULL)
	{
		ACPI_MADT_INTERRUPT_OVERRIDE *io_redirect = list_get_data_by_id(acpi_nodes, ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, 0);
		int n = 1;

		while (io_redirect != NULL)
		{
			/* Do we need to redirect? */
			if (io_redirect->SourceIrq == Irq)
			{
				/* Redirect */
				i_irq = io_redirect->GlobalIrq;

				/* Re-adjust trigger & polarity */
				i_apic &= ~(0x8000 | 0x2000);
				i_apic |= (InterruptGetPolarity(io_redirect->IntiFlags, 0) << 13);
				i_apic |= (InterruptGetTrigger(io_redirect->IntiFlags, 0) << 15);

				break;
			}

			/* Get next io redirection */
			io_redirect = list_get_data_by_id(acpi_nodes, ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, n);
			n++;
		}
	}

	/* Isa? */
	if (i_irq < X86_NUM_ISA_INTERRUPTS)
		InterruptAllocateISA(i_irq);

	/* Get correct Io Apic */
	IoApic = ApicGetIoFromGsi(i_irq);

	/* If Apic Entry is located, we need to adjust */
	CheckApicEntry = ApicReadIoEntry(IoApic, 0x10 + (2 * i_irq));
	lower = (uint32_t)(CheckApicEntry & 0xFFFFFFFF);
	upper = (uint32_t)((CheckApicEntry >> 32) & 0xFFFFFFFF);

	/* Sanity, we can't just override */
	if (!(lower & 0x10000))
	{
		/* Retrieve Idt */
		uint32_t eIdtEntry = (uint32_t)(CheckApicEntry & 0xFF);

		/* Install into table */
		InterruptInstallIdtOnly(i_irq, eIdtEntry, Callback, Args);
	}
	else
	{
		/* Install into table */
		InterruptInstallIdtOnly(i_irq, IdtEntry, Callback, Args);

		/* i_irq is the initial irq */
		ApicWriteIoEntry(IoApic, i_irq, i_apic);
	}

	/* Allocate it if ISA */
	if (i_irq < 16)
		IrqIsaTable[i_irq] = 1;
}

/* Install a normal, lowest priority interrupt */
void InterruptInstallISA(uint32_t Irq, uint32_t IdtEntry, IrqHandler_t Callback, void *Args)
{
	uint64_t apic_flags = 0;

	apic_flags = 0x0F00000000000000;	/* Target all groups */
	apic_flags |= 0x100;				/* Lowest Priority */
	apic_flags |= 0x800;				/* Logical Destination Mode */

	/* We have one ACPI Special Case */
	if (Irq == AcpiGbl_FADT.SciInterrupt)
	{
		apic_flags |= 0x2000;			/* Active Low */
		apic_flags |= 0x8000;			/* Level Sensitive */
	}

	apic_flags |= IdtEntry;			/* Interrupt Vector */

	InterruptInstallBase(Irq, IdtEntry, apic_flags, Callback, Args);
}

/* Install shared interrupt */
void InterruptInstallShared(uint32_t Irq, uint32_t IdtEntry, IrqHandler_t Callback, void *Args)
{
	/* Setup APIC flags */
	uint64_t apic_flags = 0x0F00000000000000;	/* Target all cpu groups */
	apic_flags |= 0x100;						/* Lowest Priority */
	apic_flags |= 0x800;						/* Logical Destination Mode */
	apic_flags |= (1 << 13);					/* Set Polarity */
	apic_flags |= (1 << 15);					/* Set Trigger Mode */

	/* Set IDT Vector */
	apic_flags |= IdtEntry;

	if (IrqTable[IdtEntry][0].Installed)
		InterruptInstallIdtOnly(Irq, IdtEntry, Callback, Args);
	else
		InterruptInstallBase(Irq, IdtEntry, apic_flags, Callback, Args);
}

/* Install a pci interrupt */
void InterruptInstallPci(PciDevice_t *PciDevice, IrqHandler_t Callback, void *Args)
{
	uint64_t apic_flags = 0;
	int result;
	uint8_t trigger_mode = 0, polarity = 0, shareable = 0;
	uint32_t io_entry = 0;
	uint32_t idt_entry = 0x20;
	uint32_t pin = PciDevice->Header->InterruptPin;
	uint8_t fixed = 0;
	
	/* Pin is not 0 indexed from PCI info */
	pin--;

	/* Get Interrupt Information */
	result = PciDeviceGetIrq(PciDevice->Bus, PciDevice->Device, pin,
		&trigger_mode, &polarity, &shareable, &fixed);

	/* If no routing exists use the interrupt_line */
	if (result == -1)
	{
		io_entry = PciDevice->Header->InterruptLine;
		idt_entry += PciDevice->Header->InterruptLine;

		apic_flags = 0x0F00000000000000;	/* Target all groups */
		apic_flags |= 0x100;				/* Lowest Priority */
		apic_flags |= 0x800;				/* Logical Destination Mode */
	}
	else
	{
		io_entry = (uint32_t)result;
		pin = PciDevice->Header->InterruptPin;

		/* Update PCI Interrupt Line */
		PciWriteByte(
			(const uint16_t)PciDevice->Bus, (const uint16_t)PciDevice->Device,
			(const uint16_t)PciDevice->Function, 0x3C, (uint8_t)io_entry);

		/* Setup APIC flags */
		apic_flags = 0x0F00000000000000;			/* Target all groups */
		apic_flags |= 0x100;						/* Lowest Priority */
		apic_flags |= 0x800;						/* Logical Destination Mode */

		if (io_entry >= X86_NUM_ISA_INTERRUPTS)
		{
			apic_flags |= (1 << 13);				/* Set Polarity */
			apic_flags |= (1 << 15);				/* Set Trigger Mode */
		}
		else
		{
			apic_flags |= ((polarity & 0x1) << 13);		/* Set Polarity */
			apic_flags |= ((trigger_mode & 0x1) << 15);	/* Set Trigger Mode */
		}

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

	if (IrqTable[idt_entry][0].Installed)
		InterruptInstallIdtOnly(io_entry, idt_entry, Callback, Args);
	else
		InterruptInstallBase(io_entry, idt_entry, apic_flags, Callback, Args);
}

/* Install only the interrupt handler, 
 *  this should be used for software interrupts */
void InterruptInstallIdtOnly(uint32_t Gsi, uint32_t IdtEntry, IrqHandler_t Callback, void *Args)
{
	/* Install into table */
	int i;
	int found = 0;

	/* Find a free interrupt */
	for (i = 0; i < X86_MAX_HANDLERS_PER_INTERRUPT; i++)
	{
		if (IrqTable[IdtEntry][i].Installed != 0)
			continue;

		/* Install it */
		IrqTable[IdtEntry][i].Function = Callback;
		IrqTable[IdtEntry][i].Data = Args;
		IrqTable[IdtEntry][i].Installed = 1;
		IrqTable[IdtEntry][i].Gsi = Gsi;
		found = 1;
		break;
	}

	/* Sanity */
	assert(found != 0);
}

/* Allocate ISA Interrupt */
OsStatus_t InterruptAllocateISA(uint32_t Irq)
{
	/* Sanity */
	if (Irq > 15)
		return OS_STATUS_FAIL;

	/* Allocate if free */
	if (IrqIsaTable[Irq] != 1)
	{
		IrqIsaTable[Irq] = 1;
		return OS_STATUS_OK;
	}
	
	/* Damn */
	return OS_STATUS_FAIL;
}

/* Check Irq Status */
uint32_t InterruptIrqCount(uint32_t Irq)
{
	/* Vars */
	uint32_t i, RetVal = 0;

	/* Sanity */
	if (Irq < X86_NUM_ISA_INTERRUPTS)
		return IrqIsaTable[Irq];

	/* Iterate */
	for (i = 0; i < X86_MAX_HANDLERS_PER_INTERRUPT; i++)
	{
		if (IrqTable[32 + Irq][i].Installed == 1)
			RetVal++;
	}

	/* Done */
	return RetVal;
}

/* Allocate Shareable Interrupt */
uint32_t InterruptAllocatePCI(uint32_t Irqs[], uint32_t Count)
{
	/* Vars */
	uint32_t i;
	uint32_t BestIrq = 0xFFFFFFFF;

	/* Iterate */
	for (i = 0; i < Count; i++)
	{
		/* Check count */
		uint32_t iCount = InterruptIrqCount(Irqs[i]);

		/* Is it better? */
		if (iCount < BestIrq)
			BestIrq = iCount;
	}

	/* Done */
	return BestIrq;
}

/* The common entry point for interrupts */
void InterruptEntry(Registers_t *regs)
{
	/* Determine Irq */
	int i, res = 0;
	uint32_t gsi = 0xFFFFFFFF;
	uint32_t irq = regs->Irq + 0x20;

	/* Get handler(s) */
	for (i = 0; i < X86_MAX_HANDLERS_PER_INTERRUPT; i++)
	{
		if (IrqTable[irq][i].Installed)
		{
			/* If no args are specified we give access
			* to registers */
			if (IrqTable[irq][i].Data == NULL)
				res = IrqTable[irq][i].Function((void*)regs);
			else
				res = IrqTable[irq][i].Function(IrqTable[irq][i].Data);

			/* Only one device could make interrupt */
			if (res == X86_IRQ_HANDLED)
			{
				gsi = IrqTable[irq][i].Gsi;
				break;
			}
		}
	}

	/* Send EOI (if not spurious) */
	if (irq != INTERRUPT_SPURIOUS7
		&& irq != INTERRUPT_SPURIOUS)
		ApicSendEoi(gsi, irq);
}

/* Disables interrupts and returns
* the state before disabling */
IntStatus_t InterruptDisable(void)
{
	IntStatus_t cur_state = InterruptSaveState();
	__cli();
	return cur_state;
}

/* Enables interrupts and returns
* the state before enabling */
IntStatus_t InterruptEnable(void)
{
	IntStatus_t cur_state = InterruptSaveState();
	__sti();
	return cur_state;
}

/* Restores the state to the given
* state */
IntStatus_t InterruptRestoreState(IntStatus_t state)
{
	if (state != 0)
		return InterruptEnable();
	else
		return InterruptDisable();

}

/* Gets the current interrupt state */
IntStatus_t InterruptSaveState(void)
{
	if (__getflags() & EFLAGS_INTERRUPT_FLAG)
		return 1;
	else
		return 0;
}

/* Returns whether or not interrupts are
* disabled */
IntStatus_t InterruptIsDisabled(void)
{
	return !InterruptSaveState();
}

/* Idles using HALT */
void Idle(void)
{
	__hlt();
}

/* Initiates Interrupt Handlers */
void InterruptInit(void)
{
	int i, j;

	/* Null out interrupt table */
	memset((void*)&IrqTable, 0, sizeof(IrqTable));
	memset((void*)&IrqIsaTable, 0, sizeof(IrqIsaTable));

	/* Pre-allocate some of the interrupts */
	IrqIsaTable[7] = 1; //Spurious
	IrqIsaTable[9] = 1; //SCI Interrupt

	/* Setup Stuff */
	for (i = 0; i < X86_IDT_DESCRIPTORS; i++)
	{
		for (j = 4; j < X86_MAX_HANDLERS_PER_INTERRUPT; j++)
		{
			/* Mark reserved interrupts */
			if (i < 0x20)
				IrqTable[i][j].Installed = 1;
			else
				IrqTable[i][j].Installed = 0;
		}
	}

	/* Install Irqs */
	IdtInstallDescriptor(32, (uint32_t)&irq_stringify(32), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(33, (uint32_t)&irq_stringify(33), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(34, (uint32_t)&irq_stringify(34), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(35, (uint32_t)&irq_stringify(35), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(36, (uint32_t)&irq_stringify(36), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(37, (uint32_t)&irq_stringify(37), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(38, (uint32_t)&irq_stringify(38), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(39, (uint32_t)&irq_stringify(39), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(40, (uint32_t)&irq_stringify(40), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(41, (uint32_t)&irq_stringify(41), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(42, (uint32_t)&irq_stringify(42), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(43, (uint32_t)&irq_stringify(43), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(44, (uint32_t)&irq_stringify(44), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(45, (uint32_t)&irq_stringify(45), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(46, (uint32_t)&irq_stringify(46), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(47, (uint32_t)&irq_stringify(47), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(48, (uint32_t)&irq_stringify(48), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(49, (uint32_t)&irq_stringify(49), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(50, (uint32_t)&irq_stringify(50), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(51, (uint32_t)&irq_stringify(51), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(52, (uint32_t)&irq_stringify(52), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(53, (uint32_t)&irq_stringify(53), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(54, (uint32_t)&irq_stringify(54), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(55, (uint32_t)&irq_stringify(55), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(56, (uint32_t)&irq_stringify(56), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(57, (uint32_t)&irq_stringify(57), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(58, (uint32_t)&irq_stringify(58), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(59, (uint32_t)&irq_stringify(59), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(60, (uint32_t)&irq_stringify(60), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(61, (uint32_t)&irq_stringify(61), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(62, (uint32_t)&irq_stringify(62), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(63, (uint32_t)&irq_stringify(63), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(64, (uint32_t)&irq_stringify(64), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(65, (uint32_t)&irq_stringify(65), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(66, (uint32_t)&irq_stringify(66), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(67, (uint32_t)&irq_stringify(67), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(68, (uint32_t)&irq_stringify(68), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(69, (uint32_t)&irq_stringify(69), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(70, (uint32_t)&irq_stringify(70), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(71, (uint32_t)&irq_stringify(71), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(72, (uint32_t)&irq_stringify(72), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(73, (uint32_t)&irq_stringify(73), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(74, (uint32_t)&irq_stringify(74), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(75, (uint32_t)&irq_stringify(75), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(76, (uint32_t)&irq_stringify(76), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(77, (uint32_t)&irq_stringify(77), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(78, (uint32_t)&irq_stringify(78), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(79, (uint32_t)&irq_stringify(79), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(80, (uint32_t)&irq_stringify(80), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(81, (uint32_t)&irq_stringify(81), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(82, (uint32_t)&irq_stringify(82), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(83, (uint32_t)&irq_stringify(83), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(84, (uint32_t)&irq_stringify(84), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(85, (uint32_t)&irq_stringify(85), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(86, (uint32_t)&irq_stringify(86), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(87, (uint32_t)&irq_stringify(87), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(88, (uint32_t)&irq_stringify(88), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(89, (uint32_t)&irq_stringify(89), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(90, (uint32_t)&irq_stringify(90), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(91, (uint32_t)&irq_stringify(91), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(92, (uint32_t)&irq_stringify(92), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(93, (uint32_t)&irq_stringify(93), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(94, (uint32_t)&irq_stringify(94), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(95, (uint32_t)&irq_stringify(95), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(96, (uint32_t)&irq_stringify(96), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(97, (uint32_t)&irq_stringify(97), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(98, (uint32_t)&irq_stringify(98), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(99, (uint32_t)&irq_stringify(99), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(100, (uint32_t)&irq_stringify(100), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(101, (uint32_t)&irq_stringify(101), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(102, (uint32_t)&irq_stringify(102), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(103, (uint32_t)&irq_stringify(103), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(104, (uint32_t)&irq_stringify(104), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(105, (uint32_t)&irq_stringify(105), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(106, (uint32_t)&irq_stringify(106), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(107, (uint32_t)&irq_stringify(107), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(108, (uint32_t)&irq_stringify(108), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(109, (uint32_t)&irq_stringify(109), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(110, (uint32_t)&irq_stringify(110), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(111, (uint32_t)&irq_stringify(111), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(112, (uint32_t)&irq_stringify(112), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(113, (uint32_t)&irq_stringify(113), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(114, (uint32_t)&irq_stringify(114), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(115, (uint32_t)&irq_stringify(115), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(116, (uint32_t)&irq_stringify(116), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(117, (uint32_t)&irq_stringify(117), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(118, (uint32_t)&irq_stringify(118), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(119, (uint32_t)&irq_stringify(119), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(120, (uint32_t)&irq_stringify(120), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(121, (uint32_t)&irq_stringify(121), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(122, (uint32_t)&irq_stringify(122), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(123, (uint32_t)&irq_stringify(123), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(124, (uint32_t)&irq_stringify(124), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(125, (uint32_t)&irq_stringify(125), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(126, (uint32_t)&irq_stringify(126), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);

	/* Spurious */
	IdtInstallDescriptor(127, (uint32_t)&irq_stringify(127), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);

	/* Syscalls */
	IdtInstallDescriptor(128, (uint32_t)&irq_stringify(128), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_TRAP_GATE32);

	/* Yield */
	IdtInstallDescriptor(129, (uint32_t)&irq_stringify(129), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);

	/* Next Batch */
	IdtInstallDescriptor(130, (uint32_t)&irq_stringify(130), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(131, (uint32_t)&irq_stringify(131), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(132, (uint32_t)&irq_stringify(132), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(133, (uint32_t)&irq_stringify(133), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(134, (uint32_t)&irq_stringify(134), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(135, (uint32_t)&irq_stringify(135), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(136, (uint32_t)&irq_stringify(136), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(137, (uint32_t)&irq_stringify(137), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(138, (uint32_t)&irq_stringify(138), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(139, (uint32_t)&irq_stringify(139), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(140, (uint32_t)&irq_stringify(140), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(141, (uint32_t)&irq_stringify(141), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(142, (uint32_t)&irq_stringify(142), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(143, (uint32_t)&irq_stringify(143), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(144, (uint32_t)&irq_stringify(144), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(145, (uint32_t)&irq_stringify(145), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(146, (uint32_t)&irq_stringify(146), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(147, (uint32_t)&irq_stringify(147), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(148, (uint32_t)&irq_stringify(148), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(149, (uint32_t)&irq_stringify(149), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(150, (uint32_t)&irq_stringify(150), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(151, (uint32_t)&irq_stringify(151), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(152, (uint32_t)&irq_stringify(152), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(153, (uint32_t)&irq_stringify(153), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(154, (uint32_t)&irq_stringify(154), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(155, (uint32_t)&irq_stringify(155), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(156, (uint32_t)&irq_stringify(156), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(157, (uint32_t)&irq_stringify(157), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(158, (uint32_t)&irq_stringify(158), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(159, (uint32_t)&irq_stringify(159), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(160, (uint32_t)&irq_stringify(160), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(161, (uint32_t)&irq_stringify(161), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(162, (uint32_t)&irq_stringify(162), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(163, (uint32_t)&irq_stringify(163), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(164, (uint32_t)&irq_stringify(164), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(165, (uint32_t)&irq_stringify(165), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(166, (uint32_t)&irq_stringify(166), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(167, (uint32_t)&irq_stringify(167), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(168, (uint32_t)&irq_stringify(168), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(169, (uint32_t)&irq_stringify(169), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(170, (uint32_t)&irq_stringify(170), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(171, (uint32_t)&irq_stringify(171), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(172, (uint32_t)&irq_stringify(172), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(173, (uint32_t)&irq_stringify(173), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(174, (uint32_t)&irq_stringify(174), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(175, (uint32_t)&irq_stringify(175), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(176, (uint32_t)&irq_stringify(176), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(177, (uint32_t)&irq_stringify(177), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(178, (uint32_t)&irq_stringify(178), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(179, (uint32_t)&irq_stringify(179), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(180, (uint32_t)&irq_stringify(180), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(181, (uint32_t)&irq_stringify(181), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(182, (uint32_t)&irq_stringify(182), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(183, (uint32_t)&irq_stringify(183), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(184, (uint32_t)&irq_stringify(184), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(185, (uint32_t)&irq_stringify(185), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(186, (uint32_t)&irq_stringify(186), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(187, (uint32_t)&irq_stringify(187), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(188, (uint32_t)&irq_stringify(188), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(189, (uint32_t)&irq_stringify(189), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(190, (uint32_t)&irq_stringify(190), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(191, (uint32_t)&irq_stringify(191), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(192, (uint32_t)&irq_stringify(192), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(193, (uint32_t)&irq_stringify(193), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(194, (uint32_t)&irq_stringify(194), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(195, (uint32_t)&irq_stringify(195), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(196, (uint32_t)&irq_stringify(196), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(197, (uint32_t)&irq_stringify(197), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(198, (uint32_t)&irq_stringify(198), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(199, (uint32_t)&irq_stringify(199), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(200, (uint32_t)&irq_stringify(200), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(201, (uint32_t)&irq_stringify(201), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(202, (uint32_t)&irq_stringify(202), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(203, (uint32_t)&irq_stringify(203), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(204, (uint32_t)&irq_stringify(204), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(205, (uint32_t)&irq_stringify(205), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(206, (uint32_t)&irq_stringify(206), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(207, (uint32_t)&irq_stringify(207), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(208, (uint32_t)&irq_stringify(208), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(209, (uint32_t)&irq_stringify(209), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(210, (uint32_t)&irq_stringify(210), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(211, (uint32_t)&irq_stringify(211), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(212, (uint32_t)&irq_stringify(212), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(213, (uint32_t)&irq_stringify(213), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(214, (uint32_t)&irq_stringify(214), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(215, (uint32_t)&irq_stringify(215), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(216, (uint32_t)&irq_stringify(216), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(217, (uint32_t)&irq_stringify(217), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(218, (uint32_t)&irq_stringify(218), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(219, (uint32_t)&irq_stringify(219), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(220, (uint32_t)&irq_stringify(220), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);			/* PCI Interrupt 0 */
	IdtInstallDescriptor(221, (uint32_t)&irq_stringify(221), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(222, (uint32_t)&irq_stringify(222), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(223, (uint32_t)&irq_stringify(223), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(224, (uint32_t)&irq_stringify(224), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);			/* PCI Interrupt 1 */
	IdtInstallDescriptor(225, (uint32_t)&irq_stringify(225), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(226, (uint32_t)&irq_stringify(226), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(227, (uint32_t)&irq_stringify(227), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(228, (uint32_t)&irq_stringify(228), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);			/* PCI Interrupt 2 */
	IdtInstallDescriptor(229, (uint32_t)&irq_stringify(229), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(230, (uint32_t)&irq_stringify(230), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(231, (uint32_t)&irq_stringify(231), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);

	/* Hardware Ints */
	IdtInstallDescriptor(232, (uint32_t)&irq_stringify(232), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);			/* PCI Interrupt 3 */
	IdtInstallDescriptor(233, (uint32_t)&irq_stringify(233), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(234, (uint32_t)&irq_stringify(234), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(235, (uint32_t)&irq_stringify(235), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(236, (uint32_t)&irq_stringify(236), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(237, (uint32_t)&irq_stringify(237), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(238, (uint32_t)&irq_stringify(238), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(239, (uint32_t)&irq_stringify(239), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(240, (uint32_t)&irq_stringify(240), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(241, (uint32_t)&irq_stringify(241), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(242, (uint32_t)&irq_stringify(242), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(243, (uint32_t)&irq_stringify(243), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(244, (uint32_t)&irq_stringify(244), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(245, (uint32_t)&irq_stringify(245), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(246, (uint32_t)&irq_stringify(246), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(247, (uint32_t)&irq_stringify(247), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(248, (uint32_t)&irq_stringify(248), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(249, (uint32_t)&irq_stringify(249), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(250, (uint32_t)&irq_stringify(250), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(251, (uint32_t)&irq_stringify(251), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(252, (uint32_t)&irq_stringify(252), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(253, (uint32_t)&irq_stringify(253), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(254, (uint32_t)&irq_stringify(254), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(255, (uint32_t)&irq_stringify(255), X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
}