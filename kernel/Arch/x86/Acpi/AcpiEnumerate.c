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
#include <Memory.h>
#include <Apic.h>
#include <List.h>
#include <Heap.h>
#include <Log.h>

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
				LogInformation("MADT", "Found CPU: %u (%s)", 
					AcpiCpu->Id, (AcpiCpu->LapicFlags & 0x1) ? "Active" : "Inactive");

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
			LogInformation("MADT", "Found IO-APIC: %u", AcpiIoApic->Id);

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

		} break;

		default:
			LogDebug("MADT", "Found Type %u", MadtEntry->Type);
			break;
		}

		/* Next */
		MadtEntry = (ACPI_SUBTABLE_HEADER*)
			ACPI_ADD_PTR(ACPI_SUBTABLE_HEADER, MadtEntry, MadtEntry->Length);
	}
}

/* Enumerate SRAT Entries */
void AcpiEnumerateSRAT(void *SratStart, void *SratEnd)
{
	_CRT_UNUSED(SratStart);
	_CRT_UNUSED(SratEnd);
}

/* Initializes Early Access
* and enumerates the APIC */
void AcpiEnumerate(void)
{
	/* Vars */
	ACPI_TABLE_MADT *MadtTable = NULL;
	ACPI_TABLE_SRAT *SratTable = NULL;
	ACPI_TABLE_SBST *BattTable = NULL;

	ACPI_TABLE_HEADER *Header = NULL;
	ACPI_TABLE_HEADER *Header2 = NULL;
	ACPI_TABLE_HEADER *Header3 = NULL;

	ACPI_STATUS Status = 0;

	/* Early Table Access */
	Status = AcpiInitializeTables((ACPI_TABLE_DESC*)&TableArray, ACPI_MAX_INIT_TABLES, TRUE);

	/* Sanity */
	if (ACPI_FAILURE(Status))
	{
		LogFatal("ACPI", "AcpiInitializeTables, %u!", Status);
		for (;;);
	}

	/* Get the table */
	if (ACPI_FAILURE(AcpiGetTable(ACPI_SIG_MADT, 0, &Header)))
	{
		/* Damn :( */
		LogFatal("ACPI", "Unable the locate the MADT Table");

		/* On older pc's we should parse the MP table */

		/* Stall */
		for (;;);
	}

	/* Info */
	LogInformation("ACPI", "Enumerating the MADT Table");

	/* Cast */
	MadtTable = (ACPI_TABLE_MADT*)Header;

	/* Create the acpi lists */
	GlbAcpiNodes = list_create(LIST_NORMAL);

	/* Get Local Apic Address */
	GlbNumLogicalCpus = 0;
	GlbNumIoApics = 0;

	/* Identity map it in */
	LogInformation("ACPI", "Local Apic Addr at 0x%x", MadtTable->Address);
	MmVirtualMap(NULL, MadtTable->Address, MadtTable->Address, 0x10);

	/* Now we can set it */
	GlbLocalApicAddress = MadtTable->Address;

	/* Enumerate MADT */
	AcpiEnumarateMADT((void*)((Addr_t)MadtTable + sizeof(ACPI_TABLE_MADT)), 
		(void*)((Addr_t)MadtTable + MadtTable->Header.Length));

	/* Info */
	LogInformation("ACPI", "Enumerating the SRAT Table");

	/* Enumerate SRAT */
	if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_SRAT, 0, &Header2)))
	{
		/* Cast */
		SratTable = (ACPI_TABLE_SRAT*)Header2;

		/* Gogo */
		AcpiEnumerateSRAT((void*)((Addr_t)SratTable + sizeof(ACPI_TABLE_MADT)),
			(void*)((Addr_t)SratTable + SratTable->Header.Length));
	}
	else
		LogDebug("ACPI", "Unable the locate the SRAT Table");

	/* Enumerate SBST */
	
	/* Info */
	LogInformation("ACPI", "Parsing the SBST Table");

	/* Enumerate SRAT */
	if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_SBST, 0, &Header3)))
	{
		/* Cast */
		BattTable = (ACPI_TABLE_SBST*)Header2;

		/* Gogo */
		
	}
	else
		LogDebug("ACPI", "Unable the locate the SBST Table");
}
