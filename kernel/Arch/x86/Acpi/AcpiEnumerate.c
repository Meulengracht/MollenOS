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
* MollenOS x86 ACPI Interface (Uses ACPICA)
*/

/* Includes */
#include <Arch.h>
#include <Acpi.h>
#include <Apic.h>
#include <List.h>
#include <Heap.h>
#include <stdio.h>

/* Globals */
volatile uint32_t GlbNumIoApics = 0;
list_t *GlbIoApics = NULL;
uint32_t GlbIoApicI8259Pin = 0;
uint32_t GlbIoApicI8259Apic = 0;
list_t *GlbAcpiNodes = NULL;
Addr_t GlbLocalApicAddress = 0;
uint32_t GlbNumLogicalCpus = 0;

/* Static Acpica */
#define ACPI_MAX_INIT_TABLES 16
static ACPI_TABLE_DESC TableArray[ACPI_MAX_INIT_TABLES];

/* This function redirects a IO Apic */
void AcpiSetupIoApic(ACPI_MADT_IO_APIC *ioapic, int num)
{
	/* Cast Data */
	uint32_t io_entries, i, j;
	uint8_t io_apic_num = (uint8_t)GlbNumIoApics;
	IoApic_t *IoListEntry = NULL;

	printf("    * Redirecting I/O Apic %u\n", ioapic->Id);

	/* Make sure address is mapped */
	if (!MmVirtualGetMapping(NULL, ioapic->Address))
		MmVirtualMap(NULL, ioapic->Address, ioapic->Address, 0);

	/* Allocate Entry */
	IoListEntry = (IoApic_t*)kmalloc(sizeof(IoApic_t));
	IoListEntry->GsiStart = ioapic->GlobalIrqBase;
	IoListEntry->Id = ioapic->Id;
	IoListEntry->BaseAddress = ioapic->Address;

	/* Maximum Redirection Entry—RO. This field contains the entry number (0 being the lowest
	* entry) of the highest entry in the I/O Redirection Table. The value is equal to the number of
	* interrupt input pins for the IOAPIC minus one. The range of values is 0 through 239. */
	io_entries = ApicIoRead(IoListEntry, 1);
	io_entries >>= 16;
	io_entries &= 0xFF;

	printf("    * IO Entries: %u\n", io_entries);

	/* Fill rest of info */
	IoListEntry->PinCount = io_entries + 1;
	IoListEntry->Version = 0;

	/* Add to list */
	list_append(GlbIoApics, list_create_node(ioapic->Id, IoListEntry));

	/* Structure of IO Entry Register:
	* Bits 0 - 7: Interrupt Vector that will be raised (Valid ranges are from 0x10 - 0xFE) - Read/Write
	* Bits 8 - 10: Delivery Mode. - Read / Write
	*      - 000: Fixed Delivery, deliver interrupt to all cores listed in destination.
	*      - 001: Lowest Priority, deliver interrupt to a core running lowest priority.
	*      - 010: System Management Interrupt, must be edge triggered.
	*      - 011: Reserved
	*      - 100: NMI, deliver the interrupt to NMI signal of all cores, must be edge triggered.
	*      - 101: INIT, deliver the signal to all cores by asserting init signal
	*      - 110: Reserved
	*      - 111: ExtINT, Like fixed, requires edge triggered.
	* Bit 11: Destination Mode, determines how the destination is interpreted. 0 means
	*                           phyiscal mode (we use apic id), 1 means logical mode (we use set of processors).
	* Bit 12: Delivery Status of the interrupt, read only. 0 = IDLE, 1 = Send Pending
	* Bit 13: Interrupt Pin Polarity, Read/Write, 0 = High active, 1 = Low active
	* Bit 14: Remote IRR, read only. it is set to 0 when EOI has been recieved for that interrupt
	* Bit 15: Trigger Mode, read / write, 1 = Level sensitive, 0 = Edge sensitive.
	* Bit 16: Interrupt Mask, read / write, 1 = Masked, 0 = Unmasked.
	* Bits 17 - 55: Reserved
	* Bits 56 - 63: Destination Field, if destination mode is physical, bits 56:59 should contain
	*                                   an apic id. If it is logical, bits 56:63 defines a set of
	*                                   processors that is the destination
	* */

	/* Step 1 - find the i8259 connection */
	for (i = 0; i <= io_entries; i++)
	{
		/* Read Entry */
		uint64_t Entry = ApicReadIoEntry(IoListEntry, 0x10 + (2 * i));

		/* Unmasked and ExtINT? */
		if ((Entry & 0x10700) == 0x700)
		{
			/* We found it */
			GlbIoApicI8259Pin = i;
			GlbIoApicI8259Apic = (uint32_t)io_apic_num;

			InterruptAllocateISA(i);
			break;
		}
	}

	/* Now clear interrupts */
	for (i = ioapic->GlobalIrqBase, j = 0; j <= io_entries; i++, j++)
	{
		/* Do not clear SMI! */
		uint64_t Entry = ApicReadIoEntry(IoListEntry, 0x10 + (2 * j));

		/* Sanity */
		if (Entry & 0x200)
		{
			/* Disable this interrupt for our usage */
			if (j < 16)
				InterruptAllocateISA(i);
			continue;
		}

		/* Make sure entry is masked */
		if (!(Entry & 0x10000))
		{
			Entry |= 0x10000;
			ApicWriteIoEntry(IoListEntry, j, Entry);
			Entry = ApicReadIoEntry(IoListEntry, 0x10 + (2 * j));
		}

		/* Check if Remote IRR is set */
		if (Entry & 0x4000)
		{
			/* Make sure it is set to level, otherwise we cannot clear it */
			if (!(Entry & 0x8000))
			{
				Entry |= 0x8000;
				ApicWriteIoEntry(IoListEntry, j, Entry);
			}

			/* Send EOI */
			ApicSendEoi(j, (uint32_t)(Entry & 0xFF));
		}

		/* Mask it */
		ApicWriteIoEntry(IoListEntry, j, 0x10000);
	}
}

/* Enumerate MADT Entries */
void AcpiEnumarateMADT(void *MadtStart, void *MadtEnd)
{
	ACPI_SUBTABLE_HEADER *MadtEntry;

	for (MadtEntry = (ACPI_SUBTABLE_HEADER*)MadtStart; (void *)MadtEntry < MadtEnd;)
	{
		/* Avoid an infinite loop if we hit a bogus entry. */
		if (MadtEntry->Length < sizeof(ACPI_SUBTABLE_HEADER))
			return;

		switch (MadtEntry->Type)
		{
			/* Processor Core */
			case ACPI_MADT_TYPE_LOCAL_APIC:
			{
				/* Allocate a new structure */
				ACPI_MADT_LOCAL_APIC *CpuNode = 
					(ACPI_MADT_LOCAL_APIC*)kmalloc(sizeof(ACPI_MADT_LOCAL_APIC));

				/* Cast to correct structure */
				ACPI_MADT_LOCAL_APIC *AcpiCpu = (ACPI_MADT_LOCAL_APIC*)MadtEntry;

				/* Now we have it allocated aswell, copy info */
				memcpy(CpuNode, AcpiCpu, sizeof(ACPI_MADT_LOCAL_APIC));

				/* Insert it into list */
				list_append(GlbAcpiNodes, list_create_node(ACPI_MADT_TYPE_LOCAL_APIC, CpuNode));

				/* Debug */
				printf("      > Found CPU: %u (Flags 0x%x)\n", AcpiCpu->Id, AcpiCpu->LapicFlags);

				/* Increase CPU count */
				GlbNumLogicalCpus++;

			} break;

		/* IO Apic */
		case ACPI_MADT_TYPE_IO_APIC:
		{
			/* Alocate a new structure */
			ACPI_MADT_IO_APIC *IoNode = 
				(ACPI_MADT_IO_APIC*)kmalloc(sizeof(ACPI_MADT_IO_APIC));

			/* Cast to correct structure */
			ACPI_MADT_IO_APIC *AcpiIoApic = (ACPI_MADT_IO_APIC*)MadtEntry;

			/* Now we have it allocated aswell, copy info */
			memcpy(IoNode, AcpiIoApic, sizeof(ACPI_MADT_IO_APIC));

			/* Insert it into list */
			list_append(GlbAcpiNodes, list_create_node(ACPI_MADT_TYPE_IO_APIC, IoNode));

			/* Debug */
			printf("      > Found IO-APIC: %u\n", AcpiIoApic->Id);

			/* Setup Io Apic */
			AcpiSetupIoApic(AcpiIoApic, GlbNumIoApics);

			/* Increase Count */
			GlbNumIoApics++;

		} break;

		/* Interrupt Overrides */
		case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE:
		{
			/* Allocate a new structure */
			ACPI_MADT_INTERRUPT_OVERRIDE *OverrideNode =
				(ACPI_MADT_INTERRUPT_OVERRIDE*)kmalloc(sizeof(ACPI_MADT_INTERRUPT_OVERRIDE));

			/* Cast to correct structure */
			ACPI_MADT_INTERRUPT_OVERRIDE *AcpiOverrideNode = 
				(ACPI_MADT_INTERRUPT_OVERRIDE*)MadtEntry;

			/* Now we have it allocated aswell, copy info */
			memcpy(OverrideNode, AcpiOverrideNode, sizeof(ACPI_MADT_INTERRUPT_OVERRIDE));

			/* Insert it into list */
			list_append(GlbAcpiNodes, list_create_node(ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, OverrideNode));

			/* Debug */
			printf("      > Found Interrupt Override: %u -> %u\n", 
				AcpiOverrideNode->SourceIrq, AcpiOverrideNode->GlobalIrq);

		} break;

		/* Local APIC NMI Configuration */
		case ACPI_MADT_TYPE_LOCAL_APIC_NMI:
		{
			/* Allocate a new structure */
			ACPI_MADT_LOCAL_APIC_NMI *NmiNode = 
				(ACPI_MADT_LOCAL_APIC_NMI*)kmalloc(sizeof(ACPI_MADT_LOCAL_APIC_NMI));

			/* Cast to correct structure */
			ACPI_MADT_LOCAL_APIC_NMI *ApinNmi = (ACPI_MADT_LOCAL_APIC_NMI*)MadtEntry;

			/* Now we have it allocated aswell, copy info */
			memcpy(NmiNode, ApinNmi, sizeof(ACPI_MADT_LOCAL_APIC_NMI));

			/* Insert it into list */
			list_append(GlbAcpiNodes, list_create_node(ACPI_MADT_TYPE_LOCAL_APIC_NMI, NmiNode));

			printf("      > Found Local APIC NMI: LintN %u connected to CPU %u\n", 
				ApinNmi->Lint, ApinNmi->ProcessorId);

		} break;

		default:
			printf("      > Found Type %u\n", MadtEntry->Type);
			break;
		}

		/* Next */
		MadtEntry = (ACPI_SUBTABLE_HEADER*)
			ACPI_ADD_PTR(ACPI_SUBTABLE_HEADER, MadtEntry, MadtEntry->Length);
	}
}

/* Initializes Early Access
* and enumerates the APIC */
void AcpiEnumerate(void)
{
	/* Vars */
	ACPI_TABLE_MADT *MadtTable = NULL;
	ACPI_TABLE_HEADER *Header = NULL;
	ACPI_STATUS Status = 0;

	/* Early Table Access */
	Status = AcpiInitializeTables((ACPI_TABLE_DESC*)&TableArray, ACPI_MAX_INIT_TABLES, TRUE);

	/* Sanity */
	if (ACPI_FAILURE(Status))
	{
		printf("    * FAILED, %u!\n", Status);
		for (;;);
	}

	/* Debug */
	printf("    * Acpica Enumeration Started\n");

	/* Get the table */
	if (ACPI_FAILURE(AcpiGetTable(ACPI_SIG_MADT, 0, &Header)))
	{
		/* Damn :( */
		printf("    * APIC / ACPI FAILURE, APIC TABLE DOES NOT EXIST!\n");

		/* Stall */
		for (;;);
	}

	/* Cast */
	MadtTable = (ACPI_TABLE_MADT*)Header;

	/* Create the acpi lists */
	GlbAcpiNodes = list_create(LIST_NORMAL);
	GlbIoApics = list_create(LIST_NORMAL);

	/* Get Local Apic Address */
	GlbLocalApicAddress = MadtTable->Address;
	GlbNumLogicalCpus = 0;
	GlbNumIoApics = 0;

	/* Identity map it in */
	if (!MmVirtualGetMapping(NULL, GlbLocalApicAddress))
		MmVirtualMap(NULL, GlbLocalApicAddress, GlbLocalApicAddress, 0x10);

	/* Enumerate MADT */
	AcpiEnumarateMADT((void*)((Addr_t)MadtTable + sizeof(ACPI_TABLE_MADT)), 
		(void*)((Addr_t)MadtTable + MadtTable->Header.Length));

	/* Enumerate SRAT */

	/* Enumerate ECDT */
}
