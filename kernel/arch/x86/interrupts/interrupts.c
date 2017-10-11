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
 *
 * - ISA Interrupts should be routed to boot-processor without lowest-prio?
 */
#define __MODULE		"IRQS"
#define __TRACE

/* Includes 
 * - System */
#include <system/thread.h>
#include <system/utils.h>
#include <process/phoenix.h>
#include <acpiinterface.h>
#include <interrupts.h>
#include <threading.h>
#include <memory.h>
#include <timers.h>
#include <thread.h>
#include <debug.h>
#include <heap.h>
#include <apic.h>
#include <idt.h>

/* Includes
 * - Library */
#include <ds/list.h>
#include <assert.h>
#include <stdio.h>

/* Internal definitons and helper contants */
#define EFLAGS_INTERRUPT_FLAG		(1 << 9)
#define APIC_FLAGS_DEFAULT			0x7F00000000000000

/* Externs 
 * Extern assembly functions */
__EXTERN void __cli(void);
__EXTERN void __sti(void);
__EXTERN reg_t __getflags(void);
__EXTERN reg_t __getcr2(void);
__EXTERN void init_fpu(void);
__EXTERN void load_fpu(uintptr_t *buffer);
__EXTERN void clear_ts(void);
__EXTERN void enter_thread(Context_t *Regs);

/* Externs 
 * These are for external access to some of the ACPI information */
__EXTERN List_t *GlbAcpiNodes;

/* Interrupts tables needed for
 * the x86-architecture */
MCoreInterruptDescriptor_t *InterruptTable[IDT_DESCRIPTORS];
int InterruptISATable[NUM_ISA_INTERRUPTS];
int GlbInterruptInitialized = 0;
UUId_t InterruptIdGen = 0;

/* Parses Intiflags for Polarity */
Flags_t InterruptGetPolarity(uint16_t IntiFlags, int IrqSource)
{
	/* Returning 1 means LOW, returning 0 means HIGH */
	switch (IntiFlags & ACPI_MADT_POLARITY_MASK) {
		case ACPI_MADT_POLARITY_CONFORMS: {
			if (IrqSource == (int)AcpiGbl_FADT.SciInterrupt)
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
Flags_t InterruptGetTrigger(uint16_t IntiFlags, int IrqSource)
{
	/* Returning 1 means LEVEL, returning 0 means EDGE */
	switch (IntiFlags & ACPI_MADT_TRIGGER_MASK) {
		case ACPI_MADT_TRIGGER_CONFORMS: {
			if (IrqSource == (int)AcpiGbl_FADT.SciInterrupt)
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

/* AcpiDeriveInterrupt
 * Derives an interrupt by consulting
 * the bus of the device, and spits out flags in
 * AcpiConform and returns irq */
int AcpiDeriveInterrupt(DevInfo_t Bus, 
	DevInfo_t Device, int Pin, Flags_t *AcpiConform)
{
	// Variables
	AcpiDevice_t *Dev = NULL;
	DataKey_t iKey;

	// Calculate routing index
	unsigned rIndex = (Device * 4) + (Pin - 1);
	iKey.Value = 0;

	// Trace
	TRACE("AcpiDeriveInterrupt(Bus %u, Device %u, Pin %i)",
		Bus, Device, Pin);

	// Start by checking if we can find the
	// routings by checking the given device
	Dev = AcpiLookupDevice(Bus);

	// Make sure there was a bus device for it
	if (Dev != NULL) {
		TRACE("Found bus-device <%s>, accessing index %u", 
			&Dev->HID[0], rIndex);
		if (Dev->Routings->Interrupts[rIndex].Entry != NULL) {
			PciRoutingEntry_t *pEntry = NULL;
			*AcpiConform = __DEVICEMANAGER_ACPICONFORM_PRESENT;

			// Either from list or raw
			if (Dev->Routings->InterruptInformation[rIndex] == 0) {
				pEntry = Dev->Routings->Interrupts[rIndex].Entry;
			}
			else {
				pEntry = (PciRoutingEntry_t*)
					ListGetDataByKey(Dev->Routings->Interrupts[rIndex].Entries, iKey, 0);
			}

			// Update IRQ Information
			if (pEntry->Trigger == ACPI_LEVEL_SENSITIVE) {
				*AcpiConform |= __DEVICEMANAGER_ACPICONFORM_TRIGGERMODE;
			}

			if (pEntry->Polarity == ACPI_ACTIVE_LOW) {
				*AcpiConform |= __DEVICEMANAGER_ACPICONFORM_POLARITY;
			}

			if (pEntry->Shareable != 0) {
				*AcpiConform |= __DEVICEMANAGER_ACPICONFORM_SHAREABLE;
			}

			if (pEntry->Fixed != 0) {
				*AcpiConform |= __DEVICEMANAGER_ACPICONFORM_FIXED;
			}

			// Return found interrupt
			TRACE("Found interrupt %i", pEntry->Interrupts);
			return pEntry->Interrupts;
		}
	}

	// None found
	TRACE("No interrupt found");
	return INTERRUPT_NONE;
}

/* InterruptAllocateISA
 * Allocates the ISA interrupt source, if it's 
 * already allocated it returns OsError */
OsStatus_t InterruptAllocateISA(int Source)
{
	/* Sanitize the source */
	if (Source >= NUM_ISA_INTERRUPTS) {
		return OsError;
	}

	/* Allocate if free */
	if (InterruptISATable[Source] != 1) {
		InterruptISATable[Source] = 1;
		return OsSuccess;
	}
	
	/* Damn */
	return OsError;
}

/* InterruptDetermine
 * Determines the correct APIC flags for the io-apic entry
 * from the interrupt structure */
uint64_t InterruptDetermine(MCoreInterrupt_t *Interrupt)
{
	// Variables
	uint64_t ApicFlags = APIC_FLAGS_DEFAULT;
	
	// Trace
	TRACE("InterruptDetermine()");

	// Case 1 - ISA Interrupts 
	// - Must be Edge Triggered High Active
	if (Interrupt->Line < NUM_ISA_INTERRUPTS
		&& Interrupt->Pin == INTERRUPT_NONE) {
		TRACE(" - ISA Interrupt (Active-High, Edge-Triggered)");
		ApicFlags |= 0x100;					// Lowest Priority
		ApicFlags |= 0x800;					// Logical Destination Mode
		ApicFlags |= (INTERRUPT_BASE_DEVICE + Interrupt->Line);
	}
	
	// Case 2 - PCI Interrupts (No-Pin) 
	// - Must be Level Triggered Low-Active
	else if (Interrupt->Line >= NUM_ISA_INTERRUPTS
		&& Interrupt->Pin == INTERRUPT_NONE) {
		TRACE(" - PCI Interrupt (Active-Low, Level-Triggered)");
		ApicFlags |= 0x100;						// Lowest Priority
		ApicFlags |= 0x800;						// Logical Destination Mode
		ApicFlags |= APIC_ACTIVE_LOW;			// Set Polarity
		ApicFlags |= APIC_LEVEL_TRIGGER;		// Set Trigger Mode
		ApicFlags |= (INTERRUPT_BASE_DEVICE + Interrupt->Line);
	}

	// Case 3 - PCI Interrupts (Pin) 
	// - Usually Level Triggered Low-Active
	else if (Interrupt->Pin != INTERRUPT_NONE) {
		int IdtEntry = INTERRUPT_BASE_DEVICE;

		// If no routing exists use the pci interrupt line
		if (!(Interrupt->AcpiConform & __DEVICEMANAGER_ACPICONFORM_PRESENT)) {
			TRACE(" - PCI Interrupt (Active-Low, Level-Triggered)");
			IdtEntry += Interrupt->Line;
			ApicFlags |= 0x100;					// Lowest Priority
			ApicFlags |= 0x800;					// Logical Destination Mode
		}
		else {
			TRACE(" - PCI Interrupt (Pin-Configured - 0x%x)", Interrupt->AcpiConform);
			ApicFlags |= 0x100;					// Lowest Priority
			ApicFlags |= 0x800;					// Logical Destination Mode

			// Both trigger and polarity is either fixed or set by the
			// information we extracted earlier
			if (Interrupt->Line >= NUM_ISA_INTERRUPTS) {
				ApicFlags |= APIC_ACTIVE_LOW;
				ApicFlags |= APIC_LEVEL_TRIGGER;
			}
			else {
				if (Interrupt->AcpiConform & __DEVICEMANAGER_ACPICONFORM_TRIGGERMODE) {
					ApicFlags |= APIC_LEVEL_TRIGGER;
				}
				if (Interrupt->AcpiConform & __DEVICEMANAGER_ACPICONFORM_POLARITY) {
					ApicFlags |= APIC_ACTIVE_LOW;
				}
			}

			// Add to idt-entry
			IdtEntry += Interrupt->Line;
		}

		// Set vector
		ApicFlags |= IdtEntry;
	}

	// Done
	return ApicFlags;
}

/* InterruptFinalize
 * Performs all the remaining actions, initializes the io-apic
 * entry, looks up for redirections and determines ISA stuff */
OsStatus_t InterruptFinalize(MCoreInterruptDescriptor_t *Interrupt)
{
	// Variables
	int Source = Interrupt->Source;
	uint16_t TableIndex = LOWORD(Interrupt->Id);
	uint64_t ApicFlags = APIC_FLAGS_DEFAULT;
	union {
		struct {
			uint32_t Lo;
			uint32_t Hi;
		} Parts;
		uint64_t Full;
	} ApicExisting;

	// Lookup
	ListNode_t *iNode = NULL;
	IoApic_t *IoApic = NULL;

	// Sanitize operation critical variables
	assert(TableIndex < IDT_DESCRIPTORS);

	// Trace
	TRACE("InterruptFinalize(Id %u, Source %u)", 
		Interrupt->Id, Interrupt->Source);

	// Determine the kind of flags we want to set
	ApicFlags = InterruptDetermine(&Interrupt->Interrupt);

	// Trace
	TRACE("Calculated flags for interrupt: 0x%x (TableIndex %u)", 
		LODWORD(ApicFlags), TableIndex);

	// Update table index in case
	if (TableIndex == 0) {
		Interrupt->Id |= LOBYTE(LOWORD(LODWORD(ApicFlags)));
		TableIndex = LOBYTE(LOWORD(LODWORD(ApicFlags)));
	}

	// Trace
	TRACE("Updated TableIndex: %u", TableIndex);

	// Now lookup in ACPI overrides if we should
	// change the global source
	_foreach(iNode, GlbAcpiNodes) {
		if (iNode->Key.Value == ACPI_MADT_TYPE_INTERRUPT_OVERRIDE) {
			ACPI_MADT_INTERRUPT_OVERRIDE *IoEntry =
				(ACPI_MADT_INTERRUPT_OVERRIDE*)iNode->Data;
			if ((int)IoEntry->SourceIrq == Source) {
				Interrupt->Source = Source = IoEntry->GlobalIrq;
				ApicFlags &= ~(APIC_LEVEL_TRIGGER | APIC_ACTIVE_LOW);
				ApicFlags |= (InterruptGetPolarity(IoEntry->IntiFlags, Source) << 13);
				ApicFlags |= (InterruptGetTrigger(IoEntry->IntiFlags, Source) << 15);
				break;
			}
		}
	}

	// If it's an ISA interrupt, make sure it's not
	// allocated, and if it's not, allocate it
	if (Source < NUM_ISA_INTERRUPTS) {
		if (InterruptAllocateISA(Source) != OsSuccess) {
			ERROR("Failed to allocate ISA Interrupt");
			return OsError;
		}
	}

	// Get correct Io Apic
	IoApic = ApicGetIoFromGsi(Source);

	// If Apic Entry is located, we need to adjust
	if (IoApic != NULL) {
		ApicExisting.Full = ApicReadIoEntry(IoApic, (uint32_t)Source);

		// Sanity, we can't just override the existing interrupt vector
		// so if it's already installed, we modify the table-index
		if (!(ApicExisting.Parts.Lo & APIC_MASKED)) {
			TableIndex = LOBYTE(LOWORD(ApicExisting.Parts.Lo));
			UUId_t Id = HIWORD(Interrupt->Id);
			Interrupt->Id = Id | TableIndex;
			TRACE("Updated table index for already installed interrupt: %u", 
				TableIndex);
		}
		else {
			// Unmask the irq in the io-apic
			ApicWriteIoEntry(IoApic, (uint32_t)Source, ApicFlags);
		}
	}
	else {
		ERROR("Failed to derive io-apic for source %u", Source);
	}

	// Done
	return OsSuccess;
}

/* InterruptIrqCount
 * Returns a count of all the registered
 * devices on that irq */
int InterruptIrqCount(int Source)
{
	// Variables
	MCoreInterruptDescriptor_t *Entry = NULL;
	int RetVal = 0;

	// Sanitize the requested source
	if (Source < 0 || Source > (IDT_DESCRIPTORS - 32)) {
		return INTERRUPT_NONE;
	}

	// Sanitize the ISA table
	// first, if its ISA, only one can be share
	if (Source < NUM_ISA_INTERRUPTS) {
		return InterruptISATable[Source] == 1 ? INTERRUPT_NONE : 0;
	}

	// Now iterate the linked list and find count
	Entry = InterruptTable[32 + Source];
	while (Entry != NULL) {
		RetVal++;
		Entry = Entry->Link;
	}

	// Finished
	return RetVal;
}

/* InterruptFindBest
 * Allocates the least used sharable irq
 * most useful for MSI devices */
int InterruptFindBest(int Irqs[], int Count)
{
	// Variables
	int Best = INTERRUPT_NONE;
	int i;

	// Iterate all the available irqs
	// that the device-supports
	for (i = 0; i < Count; i++) {
		if (Irqs[i] == INTERRUPT_NONE) {
			break;
		}

		// Calculate count
		int iCount = InterruptIrqCount(Irqs[i]);

		// Sanitize status, if -1
		// then its not usable
		if (iCount == INTERRUPT_NONE) {
			continue;
		}

		// Store best
		if (iCount < Best) {
			Best = iCount;
		}
	}

	// Done
	return Best;
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
	GlbInterruptInitialized = 1;
	InterruptIdGen = 0;
}

/* InterruptRegister
 * Tries to allocate the given interrupt source
 * by the given descriptor and flags. On success
 * it returns the id of the irq, and on failure it
 * returns UUID_INVALID */
UUId_t InterruptRegister(MCoreInterrupt_t *Interrupt, Flags_t Flags)
{
	// Variables
	MCoreInterruptDescriptor_t *Entry = NULL, 
		*Iterator = NULL, *Previous = NULL;
	IntStatus_t InterruptStatus = 0;
	UUId_t TableIndex = 0;
	UUId_t Id = InterruptIdGen++;

	// Sanitize initialization status
	assert(GlbInterruptInitialized == 1);

	// Trace
	TRACE("InterruptRegister(Line %u, Pin %u, Flags 0x%x)",
		Interrupt->Line, Interrupt->Pin, Flags);

	// Disable interrupts during this procedure
	InterruptStatus = InterruptDisable();

	// Allocate a new entry for the table
	Entry = (MCoreInterruptDescriptor_t*)kmalloc(sizeof(MCoreInterruptDescriptor_t));

	// Setup some initial information
	Entry->Id = (Id << 16);
	Entry->Ash = UUID_INVALID;
	Entry->Thread = ThreadingGetCurrentThreadId();
	Entry->Flags = Flags;
	Entry->Link = NULL;

	// Ok -> So we have a bunch of different cases of interrupt 
	// Software (Kernel) interrupts
	// Software (User) interrupts
	// User (fast) interrupts
	// User (slow) interrupts */
	if (Flags & INTERRUPT_KERNEL) {
		TableIndex = (UUId_t)Interrupt->Vectors[0];
		Entry->Id |= TableIndex;
		if (Flags & INTERRUPT_SOFTWARE) {
			Entry->Source = INTERRUPT_NONE;
		}
		else {
			Entry->Source = Interrupt->Line;
		}
	}
	else if (Flags & INTERRUPT_MSI) {
		// MSI interrupts are treated like software
		// interrupts
		int Vectors[INTERRUPT_BASE_DEVICE_END - INTERRUPT_BASE_DEVICE];
		int i;

		Entry->Ash = ThreadingGetCurrentThread(ApicGetCpu())->AshId;
		Entry->Source = INTERRUPT_NONE;

		// Find a suitable interrupt vector for this
		for (i = 0; i < (INTERRUPT_BASE_DEVICE_END - INTERRUPT_BASE_DEVICE); i++) {
			Vectors[i] = INTERRUPT_BASE_DEVICE + i;
		}
		TableIndex = InterruptFindBest(Vectors, 
			INTERRUPT_BASE_DEVICE_END - INTERRUPT_BASE_DEVICE);

		// Update the id
		Entry->Id |= TableIndex;

		// Fill in MSI data
		// MSI Message Address Register (0xFEE00000 LAPIC)
		// Bits 31-20: Must be 0xFEE
		// Bits 19-11: Destination ID
		// Bits 11-04: Reserved
		// Bit      3: 0 = Destination is ONE CPU, 1 = Destination is Group
		// Bit      2: Destination Mode (1 Logical, 0 Physical)
		// Bits 00-01: X
		Interrupt->MsiAddress = 0xFEE00000 | (0x0007F0000) 
			| (0x8 | 0x4);

		// Message Data Register Format
		// Bits 31-16: Reserved
		// Bit     15: Trigger Mode (1 Level, 0 Edge)
		// Bit     14: If edge, this is not used, if level, 1 = Assert, 0 = Deassert
		// Bits 13-11: Reserved
		// Bits 10-08: Delivery Mode, standard
		// Bits 07-00: Vector
		Interrupt->MsiValue = (0x100 | (TableIndex & 0xFF));
	}
	else {
		// Sanitize the line and pin first, because
		// if neither is set, choose one from the directs
		if (Flags & INTERRUPT_VECTOR) {
			Interrupt->Line = 
				InterruptFindBest(Interrupt->Vectors, INTERRUPT_MAXVECTORS);
			if (Interrupt->Line < NUM_ISA_INTERRUPTS) {
				Flags |= INTERRUPT_NOTSHARABLE;
			}
		}

		// Lookup stuff
		Entry->Ash = ThreadingGetCurrentThread(ApicGetCpu())->AshId;

		// Update the final source for this
		Entry->Source = Interrupt->Line;
	}

	// Trace
	TRACE("Updated line %u and pin %u", Interrupt->Line, Interrupt->Pin);

	// Copy interrupt information over
	memcpy(&Entry->Interrupt, Interrupt, sizeof(MCoreInterrupt_t));
	
	// Sanitize the sharable status first
	if (Flags & INTERRUPT_NOTSHARABLE) {
		if (InterruptTable[TableIndex] != NULL) {
			ERROR("Failed to install interrupt as it was not sharable");
			kfree(Entry);
			return UUID_INVALID;
		}
	}

	// Finalize the install 
	if (Entry->Source != INTERRUPT_NONE) {
		if (InterruptFinalize(Entry) != OsSuccess) {
			ERROR("Failed to install interrupt source %i", Entry->Source);
			kfree(Entry);
			return UUID_INVALID;
		}
		TableIndex = LOWORD(Entry->Id);
	}

	// First entry?
	if (InterruptTable[TableIndex] == NULL) {
		InterruptTable[TableIndex] = Entry;
	}
	else {
		Iterator = InterruptTable[TableIndex];
		while (Iterator != NULL) {
			Previous = Iterator;
			Iterator = Iterator->Link;
		}
		Previous->Link = Entry;
	}

	// Done with sensitive setup, enable interrupts
	InterruptRestoreState(InterruptStatus);

	// Trace
	TRACE("Interrupt Id 0x%x", Entry->Id);

	// Return the newly generated id
	return Entry->Id;
}

/* InterruptUnregister 
 * Unregisters the interrupt from the system and removes
 * any resources that was associated with that interrupt 
 * also masks the interrupt if it was the only user */
OsStatus_t InterruptUnregister(UUId_t Source)
{
	/* Variables we'll need */
	MCoreInterruptDescriptor_t *Entry = NULL, *Previous = NULL;
	OsStatus_t Result = OsError;
	uint16_t TableIndex = LOWORD(Source);
	int Found = 0;

	/* Sanitize the table-index */
	if (TableIndex >= IDT_DESCRIPTORS) {
		return OsError;
	}

	/* Iterate handlers in that table index 
	 * and unlink the given entry */
	Entry = InterruptTable[TableIndex];
	while (Entry != NULL) {
		if (Entry->Id == Source) {
			Found = 1;
			if (Previous == NULL) {
				InterruptTable[TableIndex] = Entry->Link;
			}
			else {
				Previous->Link = Entry->Link;
			}
			break;
		}

		/* Move on to next entry */
		Previous = Entry;
		Entry = Entry->Link;
	}

	/* Sanitize the found */
	if (Found == 0) {
		return OsError;
	}

	/* Entry is now unlinked, clean it up 
	 * mask the interrupt again if neccessary */
	if (Found == 1) {
		LogFatal("INTM", "Finish the code for unlinking interrupts!!");
		for (;;);
	}

	/* Done, return result */
	return Result;
}

/* InterruptAcknowledge 
 * Acknowledges the interrupt source and unmasks
 * the interrupt-line, allowing another interrupt
 * to occur for the given driver */
OsStatus_t InterruptAcknowledge(UUId_t Source)
{
	/* Variables we'll need */
	MCoreInterruptDescriptor_t *Entry = NULL;
	OsStatus_t Result = OsError;
	uint16_t TableIndex = LOWORD(Source);

	/* Sanitize the table-index */
	if (TableIndex >= IDT_DESCRIPTORS) {
		return OsError;
	}

	/* Iterate handlers in that table index */
	Entry = InterruptTable[TableIndex];
	while (Entry != NULL) {
		if (Entry->Id == Source) {
			ApicUnmaskGsi(Entry->Source);
			Result = OsSuccess;
			break;
		}

		/* Move on to next entry */
		Entry = Entry->Link;
	}

	/* Done, return result */
	return Result;
}

/* InterruptGet
 * Retrieves the given interrupt source information
 * as a MCoreInterruptDescriptor_t */
MCoreInterruptDescriptor_t *InterruptGet(UUId_t Source)
{
	// Variables
	MCoreInterruptDescriptor_t *Iterator = NULL;
	uint16_t TableIndex = LOWORD(Source);

	// Iterate at the correct entry
	Iterator = InterruptTable[TableIndex];
	while (Iterator != NULL) {
		if (Iterator->Id == Source) {
			return Iterator;
		}
	}

	// We didn't find it
	return NULL;
}

/* InterruptEntry
 * The common entry point for interrupts, all
 * non-exceptions will enter here, lookup a handler
 * and execute the code */
void InterruptEntry(Context_t *Registers)
{
	// Interrupts
	MCoreInterruptDescriptor_t *Entry = NULL;
	InterruptStatus_t Result = InterruptNotHandled;
	int TableIndex = (int)Registers->Irq + 32;
	int Gsi = APIC_NO_GSI;

	// Iterate handlers in that table index
	Entry = InterruptTable[TableIndex];
	while (Entry != NULL) {
		if (Entry->Flags & INTERRUPT_FAST) {
			MCoreThread_t *Thread = ThreadingGetThread(Entry->Thread);
			MCoreThread_t *Source = ThreadingGetCurrentThread(ApicGetCpu());

			/* Impersonate the target thread */
			if (Source->AddressSpace != Thread->AddressSpace) {
				IThreadImpersonate(Thread);
			}

			/* Call the fast handler */
			Result = Entry->Interrupt.FastHandler(Entry->Interrupt.Data);

			/* Restore to our own context */
			if (Source->AddressSpace != Thread->AddressSpace) {
				IThreadImpersonate(Source);
			}

			// If it was handled
			// - Register it with system stuff
			// - System timers must be registered as fast-handlers
			if (Result == InterruptHandled) {
				TimersInterrupt(Entry->Id);
				Gsi = Entry->Source;
				break;
			}
		}
		else if (Entry->Flags & INTERRUPT_KERNEL) {
			if (Entry->Interrupt.Data == NULL) {
				Result = Entry->Interrupt.FastHandler((void*)Registers);
			}
			else {
				Result = Entry->Interrupt.FastHandler(Entry->Interrupt.Data);
			}

			// If it was handled we can break
			// early as there is no need to check rest
			if (Result == InterruptHandled) {
				Gsi = Entry->Source;
				break;
			}
		}
		else {
			// Trace
			TRACE("Interrupt 0x%x for process %u is being dispatched",
				Entry->Ash, Entry->Id);

			// Send a interrupt-event to this
			__KernelInterruptDriver(Entry->Ash, Entry->Id, Entry->Interrupt.Data);
			
			// Mark as handled, so we don't spit out errors
			Result = InterruptHandled;

			// Mask the GSI and store it
			ApicMaskGsi(Entry->Source);
			Gsi = Entry->Source;
		}

		// Move on to next entry
		Entry = Entry->Link;
	}

	// Sanitize the result of the
	// irq-handling - all irqs must be handled
	if (Result != InterruptHandled) {
		FATAL(FATAL_SCOPE_KERNEL, "Unhandled interrupt %u", TableIndex);
	}
	else if (Gsi > 0 && Gsi < 16 && Gsi != 8 && Gsi != 2) {
		//TRACE("Interrupt %u (Gsi %i) was handled", TableIndex, Gsi);
	}

	// Send EOI (if not spurious)
	if (TableIndex != INTERRUPT_SPURIOUS7
		&& TableIndex != INTERRUPT_SPURIOUS) {
		ApicSendEoi(Gsi, TableIndex);
	}
}

/* ExceptionEntry
 * Common entry for all exceptions */
void ExceptionEntry(Context_t *Registers)
{
	// Variables
	MCoreThread_t *cThread = NULL;
	x86Thread_t *cT86 = NULL;
	uintptr_t Address = __MASK;
	int IssueFixed = 0;
	UUId_t Cpu;

	// Handle IRQ
	if (Registers->Irq == 0) {		// Divide By Zero

	}
	else if (Registers->Irq == 1) { // Single Step
		if (DebugSingleStep(Registers) == OsSuccess) {
			// Re-enable single-step
		}
		IssueFixed = 1;
	}
	else if (Registers->Irq == 2) { // NMI
		
	}
	else if (Registers->Irq == 3) { // Breakpoint
		DebugBreakpoint(Registers);
		IssueFixed = 1;
	}
	else if (Registers->Irq == 4) { // Overflow

	}
	else if (Registers->Irq == 5) { // Bound Range Exceeded

	}
	else if (Registers->Irq == 6) { // Invalid Opcode

	}
	else if (Registers->Irq == 7) { // DeviceNotAvailable 

		// Lookup variables
		Cpu = CpuGetCurrentId();
		cThread = ThreadingGetCurrentThread(Cpu);

		// Important asserts
		assert(cThread != NULL);

		// Get the x86 specific details
		cT86 = (x86Thread_t*)cThread->ThreadData;

		// Clear the task-switch bit
		clear_ts();

		// Either of two cases;
		// 1 - We need to initialize the FPU
		// 2 - We need to load the FPU
		if (!(cT86->Flags & X86_THREAD_FPU_INITIALISED)) {
			init_fpu();
			cT86->Flags |= X86_THREAD_FPU_INITIALISED;
			IssueFixed = 1;
		}
		else if (!(cT86->Flags & X86_THREAD_USEDFPU)) {
			load_fpu(cT86->FpuBuffer);
			cT86->Flags |= X86_THREAD_USEDFPU;
			IssueFixed = 1;
		}
	}
	else if (Registers->Irq == 8) { // Double Fault

	}
	else if (Registers->Irq == 9) { // Coprocessor Segment Overrun (Obsolete)

	}
	else if (Registers->Irq == 10) { // Invalid TSS

	}
	else if (Registers->Irq == 11) { // Segment Not Present

	}
	else if (Registers->Irq == 12) { // Stack Segment Fault

	}
	else if (Registers->Irq == 13) { // General Protection Fault

	}
	else if (Registers->Irq == 14) {	// Page Fault
		Address = (uintptr_t)__getcr2();

		// The first thing we must check before propegating events
		// is that we must check special locations
		if (Address == MEMORY_LOCATION_SIGNAL_RET) {
			SignalReturn();

			// If we reach here, no more signals, 
			// and we should just enter the actual thread
			if (cThread->Flags != THREADING_KERNELMODE) {
				enter_thread(((x86Thread_t*)cThread->ThreadData)->UserContext);
			}	
			else {
				enter_thread(((x86Thread_t*)cThread->ThreadData)->Context);
			}

			// Never reach beyond here
			FATAL(FATAL_SCOPE_KERNEL, "REACHED BEYOND enter_thread AFTER SIGNAL");
		}

		// Next step is to check whether or not the address is already
		// mapped, because then it's due to accessibility
		if (MmVirtualGetMapping(NULL, Address) != 0) {
			FATAL(FATAL_SCOPE_KERNEL, "Page fault at address 0x%x, but page is already mapped, invalid access. (User tried to access kernel memory ex).");
		}

		// Final step is to see if kernel can handle the 
		// unallocated address
		if (DebugPageFault(Registers, Address) == OsSuccess) {
			IssueFixed = 1;
		}
	}

	// Was the exception handled?
	if (IssueFixed == 0) {
		LogRedirect(LogConsole);

		// Was it a page-fault?
		if (Address != __MASK) {
			LogDebug(__MODULE, "CR2 Address: 0x%x", Address);
			char *Name = NULL;
			uintptr_t Base = 0;
			if (DebugGetModuleByAddress(Registers->Eip, &Base, &Name) == OsSuccess) {
				uintptr_t Diff = Registers->Eip - Base;
				LogDebug(__MODULE, "Fauly Address: 0x%x (%s)", Diff, Name);
			}
			else {
				LogDebug(__MODULE, "Faulty Address: 0x%x", Registers->Eip);
			}
		}

		// Enter panic handler
        DebugContext(Registers);
		DebugPanic(FATAL_SCOPE_KERNEL, __MODULE,
			"Unhandled or fatal interrupt %u, Error Code: %u, Faulty Address: 0x%x",
            Registers->Irq, Registers->ErrorCode, Registers->Eip);
	}
}

/* InterruptDisable
 * Disables interrupts and returns
 * the state before disabling */
IntStatus_t InterruptDisable(void)
{
	// Variables
	IntStatus_t CurrentState;

	// Save status
	CurrentState = InterruptSaveState();

	// Disable interrupts and return
	__cli();
	return CurrentState;
}

/* InterruptEnable
 * Enables interrupts and returns 
 * the state before enabling */
IntStatus_t InterruptEnable(void)
{
	// Variables
	IntStatus_t CurrentState;

	// Save current status
	CurrentState = InterruptSaveState();

	// Enable interrupts and return
	__sti();
	return CurrentState;
}

/* InterruptRestoreState
 * Restores the interrupt-status to the given
 * state, that must have been saved from SaveState */
IntStatus_t InterruptRestoreState(IntStatus_t State)
{
	if (State != 0) {
		return InterruptEnable();
	}
	else {
		return InterruptDisable();
	}
}

/* InterruptSaveState
 * Retrieves the current state of interrupts */
IntStatus_t InterruptSaveState(void)
{
	if (__getflags() & EFLAGS_INTERRUPT_FLAG) {
		return 1;
	}
	else {
		return 0;
	}
}

/* InterruptIsDisabled
 * Returns 1 if interrupts are currently
 * disabled or 0 if interrupts are enabled */
int InterruptIsDisabled(void)
{
	/* Just negate this state */
	return !InterruptSaveState();
}
