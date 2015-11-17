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
#include <AcpiSys.h>
#include <Apic.h>
#include <List.h>
#include <Heap.h>
#include <stdio.h>

/* Globals */
volatile Addr_t GlbLocalApicAddress = 0;
list_t *GlbAcpiNodes = NULL;
uint32_t GlbNumLogicalCpus = 0;
uint32_t GlbNumIoApics = 0;

/* Static Acpica */
#define ACPI_MAX_INIT_TABLES 16
static ACPI_TABLE_DESC TableArray[ACPI_MAX_INIT_TABLES];

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
