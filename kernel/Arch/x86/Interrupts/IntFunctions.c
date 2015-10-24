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

/* Includes */
#include <Interrupts.h>
#include <List.h>
#include <Idt.h>
#include <Pci.h>
#include <acpi.h>
#include <Apic.h>
#include <assert.h>
#include <stdio.h>

/* Internal Defines */
#define EFLAGS_INTERRUPT_FLAG (1 << 9)

/* Externs */
extern void __cli(void);
extern void __sti(void);
extern uint32_t __getflags(void);
extern list_t *GlbAcpiNodes;

extern IrqEntry_t IrqTable[X86_IDT_DESCRIPTORS][X86_MAX_HANDLERS_PER_INTERRUPT];
extern uint32_t IrqIsaTable[X86_NUM_ISA_INTERRUPTS];

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
	if (GlbAcpiNodes != NULL)
	{
		ACPI_MADT_INTERRUPT_OVERRIDE *io_redirect = 
			list_get_data_by_id(GlbAcpiNodes, ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, 0);
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
			io_redirect = list_get_data_by_id(GlbAcpiNodes, ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, n);
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
}

/* ISA Interrupts will go to BSP */
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

	if (res == 0)
		printf("Unhandled Irq 0x%x\n", irq);

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