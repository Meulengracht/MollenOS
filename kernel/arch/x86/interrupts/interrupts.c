/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS Interrupt Interface (X86)
 * - Contains the shared kernel interrupt interface
 *   that all sub-layers must conform to
 */

/* Includes 
 * - System */
#include <acpiinterface.h>
#include <interrupts.h>
#include <threading.h>
#include <thread.h>
#include <idt.h>
#include <pci.h>
#include <apic.h>
#include <log.h>

/* Includes
 * - Library */
#include <assert.h>
#include <stdio.h>
#include <ds/list.h>

/* Internal Defines */
#define EFLAGS_INTERRUPT_FLAG (1 << 9)

/* Externs */
extern void __cli(void);
extern void __sti(void);
extern uint32_t __getflags(void);
extern List_t *GlbAcpiNodes;

/* Interrupts tables needed for
 * the x86-architecture */
MCoreInterruptDescriptor_t *InterruptTable[IDT_DESCRIPTORS];
int InterruptISATable[NUM_ISA_INTERRUPTS];
UUId_t InterruptIdGen = 0;

/* Parses Intiflags for Polarity */
Flags_t InterruptGetPolarity(uint16_t IntiFlags, uint8_t IrqSource)
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
Flags_t InterruptGetTrigger(uint16_t IntiFlags, uint8_t IrqSource)
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

/* Pin conversion from behind a bridge */
int AcpiDerivePin(int Device, int Pin) {
	return (((Pin - 1) + Device) % 4) + 1;
}

/* Get Irq by Bus / Dev / Pin
* Returns -1 if no overrides exists */
int AcpiDeviceGetIrq(void *PciDevice, int Pin,
	uint8_t *TriggerMode, uint8_t *Polarity, uint8_t *Shareable,
	uint8_t *Fixed)
{
	/* Locate correct bus */
	AcpiDevice_t *Dev = NULL;
	PciDevice_t *PciDev = (PciDevice_t*)PciDevice;
	int pDevice = PciDev->Device, pPin = Pin;
	DataKey_t iKey;

	/* Calculate routing index */
	int rIndex = (pDevice * 4) + Pin;

	/* Set Key */
	iKey.Value = 0;

	/* Start by checking if we can find the
	 * routings by checking the given device */
	Dev = AcpiLookupDevice(PciDev->Bus);

	/* Sanitize */
	if (Dev != NULL) {
		/* Sanity */
		if (Dev->Routings->Interrupts[rIndex].Entry != NULL) {

			/* Extract the entry */
			PciRoutingEntry_t *pEntry = NULL;

			/* Either from list or raw */
			if (Dev->Routings->InterruptInformation[rIndex] == 0) {
				pEntry = Dev->Routings->Interrupts[rIndex].Entry;
			}
			else {
				pEntry = (PciRoutingEntry_t*)
					ListGetDataByKey(Dev->Routings->Interrupts[rIndex].Entries, iKey, 0);
			}

			/* Update IRQ Information */
			if (pEntry->Trigger == ACPI_LEVEL_SENSITIVE)
				*TriggerMode = 1;
			else
				*TriggerMode = 0;

			if (pEntry->Polarity == ACPI_ACTIVE_LOW)
				*Polarity = 1;
			else
				*Polarity = 0;

			*Shareable = pEntry->Shareable;
			*Fixed = pEntry->Fixed;
			return pEntry->Interrupts;
		}
	}

	/* Damn, check parents */
	PciDev = PciDev->Parent;
	while (PciDev) {

		/* Correct the pin */
		pPin = AcpiDerivePin(pDevice, pPin);
		pDevice = PciDev->Device;

		/* Calculate new corrected routing index */
		rIndex = (pDevice * 4) + pPin;

		/* Start by checking if we can find the
		* routings by checking the given device */
		Dev = AcpiLookupDevice(PciDev->Bus);

		/* Sanitize */
		if (Dev != NULL) {
			/* Sanity */
			if (Dev->Routings->Interrupts[rIndex].Entry != NULL) {

				/* Extract the entry */
				PciRoutingEntry_t *pEntry = NULL;

				/* Either from list or raw */
				if (Dev->Routings->InterruptInformation[rIndex] == 0) {
					pEntry = Dev->Routings->Interrupts[rIndex].Entry;
				}
				else {
					pEntry = (PciRoutingEntry_t*)
						ListGetDataByKey(Dev->Routings->Interrupts[rIndex].Entries, iKey, 0);
				}

				/* Update IRQ Information */
				if (pEntry->Trigger == ACPI_LEVEL_SENSITIVE)
					*TriggerMode = 1;
				else
					*TriggerMode = 0;

				if (pEntry->Polarity == ACPI_ACTIVE_LOW)
					*Polarity = 1;
				else
					*Polarity = 0;

				*Shareable = pEntry->Shareable;
				*Fixed = pEntry->Fixed;
				return pEntry->Interrupts;
			}
		}
	}

	return -1;
}

/* Install a interrupt handler */
void InterruptInstallBase(uint32_t Irq, uint32_t IdtEntry, uint64_t ApicEntry, IrqHandler_t Callback, void *Args)
{
	/* Determine Correct Irq */
	uint32_t RealIrq = Irq;
	uint64_t i_apic = ApicEntry;
	uint64_t CheckApicEntry = 0;
	uint32_t upper = 0;
	uint32_t lower = 0;
	IoApic_t *IoApic;
	DataKey_t Key;

	/* Sanity */
	assert(Irq < X86_IDT_DESCRIPTORS);

	/* Uh, check for ACPI redirection */
	if (GlbAcpiNodes != NULL)
	{
		Key.Value = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE;
		ACPI_MADT_INTERRUPT_OVERRIDE *io_redirect = ListGetDataByKey(GlbAcpiNodes, Key, 0);
		int n = 1;

		while (io_redirect != NULL)
		{
			/* Do we need to redirect? */
			if (io_redirect->SourceIrq == Irq)
			{
				/* Redirect */
				RealIrq = io_redirect->GlobalIrq;

				/* Re-adjust trigger & polarity */
				i_apic &= ~(0x8000 | 0x2000);
				i_apic |= (InterruptGetPolarity(io_redirect->IntiFlags, LOBYTE(Irq)) << 13);
				i_apic |= (InterruptGetTrigger(io_redirect->IntiFlags, LOBYTE(Irq)) << 15);

				break;
			}

			/* Get next io redirection */
			io_redirect = ListGetDataByKey(GlbAcpiNodes, Key, n);
			n++;
		}
	}

	/* Isa? */
	if (RealIrq < X86_NUM_ISA_INTERRUPTS)
		InterruptAllocateISA(RealIrq);

	/* Get correct Io Apic */
	IoApic = ApicGetIoFromGsi(RealIrq);

	/* If Apic Entry is located, we need to adjust */
	CheckApicEntry = ApicReadIoEntry(IoApic, RealIrq);
	lower = (uint32_t)(CheckApicEntry & 0xFFFFFFFF);
	upper = (uint32_t)((CheckApicEntry >> 32) & 0xFFFFFFFF);

	/* Sanity, we can't just override */
	if (!(lower & 0x10000))
	{
		/* Retrieve Idt */
		uint32_t eIdtEntry = (uint32_t)(CheckApicEntry & 0xFF);

		/* Install into table */
		InterruptInstallIdtOnly((int)RealIrq, eIdtEntry, Callback, Args);
	}
	else
	{
		/* Install into table */
		InterruptInstallIdtOnly((int)RealIrq, IdtEntry, Callback, Args);

		/* i_irq is the initial irq */
		ApicWriteIoEntry(IoApic, RealIrq, i_apic);
	}
}

/* ISA Interrupts will go to BSP */
void InterruptInstallISA(uint32_t Irq, uint32_t IdtEntry, IrqHandler_t Callback, void *Args)
{
	/* Build APIC Flags */
	uint64_t ApicFlags = 0;

	ApicFlags = 0x7F00000000000000;	/* Target all groups */
	ApicFlags |= 0x100;				/* Lowest Priority */
	ApicFlags |= 0x800;				/* Logical Destination Mode */
	ApicFlags |= IdtEntry;			/* Interrupt Vector */

	/* Install to base */
	InterruptInstallBase(Irq, IdtEntry, ApicFlags, Callback, Args);
}

/* Install only the interrupt handler, 
 *  this should be used for software interrupts */
void InterruptInstallIdtOnly(int Gsi, uint32_t IdtEntry, IrqHandler_t Callback, void *Args)
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
		return OsError;

	/* Allocate if free */
	if (IrqIsaTable[Irq] != 1)
	{
		IrqIsaTable[Irq] = 1;
		return OsNoError;
	}
	
	/* Damn */
	return OsError;
}

/* Check Irq Status */
int InterruptIrqCount(int Irq)
{
	/* Vars */
	int i, RetVal = 0;

	/* Sanity */
	if (Irq < X86_NUM_ISA_INTERRUPTS)
		return IrqIsaTable[Irq] == 1 ? -1 : 0;

	/* Iterate */
	for (i = 0; i < X86_MAX_HANDLERS_PER_INTERRUPT; i++) {
		if (IrqTable[32 + Irq][i].Installed == 1)
			RetVal++;
	}

	if (RetVal == X86_MAX_HANDLERS_PER_INTERRUPT)
		return -1;

	/* Done */
	return RetVal;
}

/* Allocate Shareable Interrupt */
int InterruptFindBest(int Irqs[], int Count)
{
	/* Vars */
	int i;
	int BestIrq = -1;

	/* Iterate */
	for (i = 0; i < Count; i++)
	{
		/* Sanity? */
		if (Irqs[i] == -1)
			break;

		/* Check count */
		int iCount = InterruptIrqCount(Irqs[i]);

		/* Sanity - Unusable */
		if (iCount == -1)
			continue;

		/* Is it better? */
		if (iCount < BestIrq)
			BestIrq = iCount;
	}

	/* Done */
	return BestIrq;
}

/* Allocate interrupt for device */
int DeviceAllocateInterrupt(void *mCoreDevice)
{
	/* Cast */
	MCoreDevice_t *Device = (MCoreDevice_t*)mCoreDevice;

	/* Setup initial Apic Flags */
	uint64_t ApicFlags = 0x7F00000000000000;	/* Target all cpu groups */

	/* Determine what kind of interrupt is needed */
	if (Device->IrqLine == -1
		&& Device->IrqPin == -1)
	{
		/* Choose from available irq's */
		Device->IrqLine = InterruptFindBest(Device->IrqAvailable, DEVICEMANAGER_MAX_IRQS);
	}
	
	/* Now actually do the allocation */
	if (Device->IrqLine < X86_NUM_ISA_INTERRUPTS
		&& Device->IrqPin == -1)
	{
		/* Request of a ISA interrupt */
		int IdtEntry = INTERRUPT_DEVICE_BASE + Device->IrqLine;

		/* Sanity */
		if (IrqIsaTable[Device->IrqLine] == 1) {
			LogFatal("SYST", "Interrupt %u is already allocated!!", Device->IrqLine);
			return -1;
		}

		ApicFlags |= 0x100;				/* Lowest Priority */
		ApicFlags |= 0x800;				/* Logical Destination Mode */
		ApicFlags |= IdtEntry;

		/* Install */
		InterruptInstallBase(Device->IrqLine, IdtEntry, ApicFlags, Device->IrqHandler, Device);

		/* Done! */
		return 0;
	}
	else if (Device->IrqLine >= X86_NUM_ISA_INTERRUPTS
		&& Device->IrqPin == -1)
	{
		/* Setup APIC flags */
		int IdtEntry = 0;
		ApicFlags |= 0x100;						/* Lowest Priority */
		ApicFlags |= 0x800;						/* Logical Destination Mode */
		ApicFlags |= (1 << 13);					/* Set Polarity */
		ApicFlags |= (1 << 15);					/* Set Trigger Mode */

		if (Device->Type == DeviceTimer)
			IdtEntry = INTERRUPT_TIMER_BASE + Device->IrqLine;
		else
			IdtEntry = INTERRUPT_DEVICE_BASE + Device->IrqLine;

		/* Set IDT Vector */
		ApicFlags |= IdtEntry;

		/* Is there already a handler? */
		if (IrqTable[IdtEntry][0].Installed)
			InterruptInstallIdtOnly(Device->IrqLine, IdtEntry, Device->IrqHandler, Device);
		else
			InterruptInstallBase(Device->IrqLine, IdtEntry, ApicFlags, Device->IrqHandler, Device);

		/* Done */
		return 0;
	}
	else if (Device->IrqPin != -1)
	{
		/* Vars, we need ACPI to determine a pin interrupt */
		int DidExist = 0, ReducedPin = Device->IrqPin;

		/* For irq information */
		uint8_t IrqTriggerMode = 0, IrqPolarity = 0;
		uint8_t IrqShareable = 0, Fixed = 0;

		/* Idt, Irq entry */
		int IrqLine = 0;
		int IdtEntry = INTERRUPT_DEVICE_BASE;

		/* Pin is not 0 indexed from PCI info */
		ReducedPin--;

		/* Get Interrupt Information */
		DidExist = AcpiDeviceGetIrq(Device->BusDevice, ReducedPin,
			&IrqTriggerMode, &IrqPolarity, &IrqShareable, &Fixed);

		/* If no routing exists use the interrupt_line */
		if (DidExist == -1)
		{
			Device->IrqLine = IrqLine = 
				(int)PciRead8(Device->Bus, Device->Device, Device->Function, 0x3C);
			IdtEntry += Device->IrqLine;

			ApicFlags |= 0x100;				/* Lowest Priority */
			ApicFlags |= 0x800;				/* Logical Destination Mode */
		}
		else
		{
			/* Update */
			Device->IrqLine = IrqLine = DidExist;

			/* Update PCI Interrupt Line */
			PciWrite8(Device->Bus, Device->Device, Device->Function, 0x3C, (uint8_t)IrqLine);

			/* Setup APIC flags */
			ApicFlags |= 0x100;						/* Lowest Priority */
			ApicFlags |= 0x800;						/* Logical Destination Mode */

			/* Both trigger and polarity is either fixed or set by the
			 * information we extracted earlier */
			if (IrqLine >= X86_NUM_ISA_INTERRUPTS)
			{
				ApicFlags |= (1 << 13);
				ApicFlags |= (1 << 15);
			}
			else
			{
				ApicFlags |= ((IrqPolarity & 0x1) << 13);		/* Set Polarity */
				ApicFlags |= ((IrqTriggerMode & 0x1) << 15);	/* Set Trigger Mode */
			}

			/* Calculate idt */
			IdtEntry += IrqLine;
		}

		/* Set IDT Vector */
		ApicFlags |= IdtEntry;

		if (IrqTable[IdtEntry][0].Installed)
			InterruptInstallIdtOnly(IrqLine, IdtEntry, Device->IrqHandler, Device);
		else
			InterruptInstallBase(IrqLine, IdtEntry, ApicFlags, Device->IrqHandler, Device);

		/* Done */
		return 0;
	}
	else
		return -4;
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

/* InterruptInitialize
 * Initializes the interrupt-manager code
 * and initializes all the resources for
 * allocating and freeing interrupts */
void InterruptInitialize(void)
{
	/* Null out interrupt tables */
	memset((void*)InterruptTable, 0, sizeof(MCoreInterruptDescriptor_t*) * IDT_DESCRIPTORS);
	memset((void*)&InterruptISATable, 0, sizeof(InterruptISATable));
	InterruptIdGen = 0;
}

/* InterruptRegister
 * Tries to allocate the given interrupt source
 * by the given descriptor and flags. On success
 * it returns the id of the irq, and on failure it
 * returns UUID_INVALID */
UUId_t InterruptRegister(MCoreInterrupt_t *Interrupt, Flags_t Flags)
{
	/* Clear interrupt_kernel flag if it's not a kernel thread */
}

/* InterruptUnregister 
 * Unregisters the interrupt from the system and removes
 * any resources that was associated with that interrupt 
 * also masks the interrupt if it was the only user */
OsStatus_t InterruptUnregister(UUId_t Source)
{

}

/* InterruptEntry
 * The common entry point for interrupts, all
 * non-exceptions will enter here, lookup a handler
 * and execute the code */
void InterruptEntry(Registers_t *Registers)
{
	/* Variables */
	MCoreInterruptDescriptor_t *Entry = NULL;
	InterruptStatus_t Result = InterruptNotHandled;
	int TableIndex = (int)Registers->Irq + 32;
	int Gsi = APIC_NO_GSI;

	/* Iterate handlers in that table index */
	Entry = InterruptTable[TableIndex];
	while (Entry != NULL) {
		if (Entry->Flags & INTERRUPT_FAST) 
		{
			/* Lookup the target thread */
			MCoreThread_t *Thread = ThreadingGetThread(Entry->Thread);
			AddressSpace_t *Current = AddressSpaceGetCurrent();

			/* Switch address space + io-map */
			if (Current != Thread->AddressSpace) {
				
			}

			/* Call the fast handler */
			Result = Entry->Interrupt.FastHandler(Entry->Interrupt.Data);

			/* Restore address space + io-map */
			if (Current != Thread->AddressSpace) {

			}
		}
		else if (Entry->Flags & INTERRUPT_KERNEL) {
			if (Entry->Interrupt.Data == NULL) {
				Result = Entry->Interrupt.FastHandler((void*)Registers);
			}
			else {
				Result = Entry->Interrupt.FastHandler(Entry->Interrupt.Data);
			}
		}
		else {
			/* Send a interrupt-event to this */
			
			/* Mark as handled, so we don't spit out errors */
			Result = InterruptHandled;
		}

		/* Was it handled? 
		 * - If entry is non-fast, non-kernel
		 *   we don't break */
		if (!(Entry->Flags & (INTERRUPT_FAST | INTERRUPT_KERNEL))) {
			if (Result == InterruptHandled) {
				Gsi = Entry->Source;
				break;
			}
		}

		/* Move on to next entry */
		Entry = Entry->Link;
	}

	/* Sanitize the result of the
	 * irq-handling */
	if (Result != InterruptNotHandled) {
		LogFatal("IRQS", "Unhandled interrupt %u", TableIndex);
	}

	/* Send EOI (if not spurious) */
	if (TableIndex != INTERRUPT_SPURIOUS7
		&& TableIndex != INTERRUPT_SPURIOUS) {
		ApicSendEoi(Gsi, TableIndex);
	}
}
