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
#include <arch.h>
#include <acpiinterface.h>
#include <heap.h>
#include <log.h>

/* C-Library */
#include <ds/list.h>

/* Globals */
List_t *GlbAcpiNodes = NULL;
int GlbAcpiAvailable = ACPI_NOT_AVAILABLE;

/* Static Acpica */
static ACPI_TABLE_DESC TableArray[ACPI_MAX_INIT_TABLES];

/* Enumerate MADT Entries */
void AcpiEnumarateMADT(void *MadtStart, void *MadtEnd)
{
	/* Variables */
	ACPI_SUBTABLE_HEADER *MadtEntry;
	DataKey_t Key;

	for (MadtEntry = (ACPI_SUBTABLE_HEADER*)MadtStart; 
		(void *)MadtEntry < MadtEnd;)
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
				Key.Value = ACPI_MADT_TYPE_LOCAL_APIC;
				ListAppend(GlbAcpiNodes, ListCreateNode(Key, Key, CpuNode));

				/* Debug */
				LogInformation("MADT", "Found CPU: %u (%s)", 
					AcpiCpu->Id, (AcpiCpu->LapicFlags & 0x1) ? "Active" : "Inactive");

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
			Key.Value = ACPI_MADT_TYPE_IO_APIC;
			ListAppend(GlbAcpiNodes, ListCreateNode(Key, Key, IoNode));

			/* Debug */
			LogInformation("MADT", "Found IO-APIC: %u", AcpiIoApic->Id);

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
			Key.Value = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE;
			ListAppend(GlbAcpiNodes, ListCreateNode(Key, Key, OverrideNode));

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
			Key.Value = ACPI_MADT_TYPE_LOCAL_APIC_NMI;
			ListAppend(GlbAcpiNodes, ListCreateNode(Key, Key, NmiNode));

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
int AcpiEnumerate(void)
{
	/* Variables needed for initial
	 * ACPI access */
	ACPI_TABLE_HEADER *Header = NULL;
	ACPI_STATUS Status = 0;

	/* Early Table Access */
	Status = AcpiInitializeTables((ACPI_TABLE_DESC*)&TableArray, ACPI_MAX_INIT_TABLES, TRUE);

	/* Sanity 
	 * If this fails there is no ACPI on the system */
	if (ACPI_FAILURE(Status)) {
		LogFatal("ACPI", "Failed to initialize early ACPI access,"
			"probable no ACPI available (%u)", Status);
		GlbAcpiAvailable = ACPI_NOT_AVAILABLE;
		return -1;
	}
	else
		GlbAcpiAvailable = ACPI_AVAILABLE;

	/* Create the acpi lists */
	GlbAcpiNodes = ListCreate(KeyInteger, LIST_NORMAL);

	/* Get the table */
	if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_MADT, 0, &Header)))
	{
		/* Variables */
		ACPI_TABLE_MADT *MadtTable = NULL;

		/* Yay! */
		LogInformation("ACPI", "Enumerating the MADT Table");

		/* Cast */
		MadtTable = (ACPI_TABLE_MADT*)Header;

		/* Enumerate MADT */
		AcpiEnumarateMADT((void*)((uintptr_t)MadtTable + sizeof(ACPI_TABLE_MADT)),
			(void*)((uintptr_t)MadtTable + MadtTable->Header.Length));
	}

	/* Enumerate SRAT */
	if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_SRAT, 0, &Header)))
	{
		/* Variables */
		ACPI_TABLE_SRAT *SratTable = NULL;

		/* Info */
		LogInformation("ACPI", "Enumerating the SRAT Table");

		/* Cast */
		SratTable = (ACPI_TABLE_SRAT*)Header;

		/* Gogo */
		AcpiEnumerateSRAT((void*)((uintptr_t)SratTable + sizeof(ACPI_TABLE_MADT)),
			(void*)((uintptr_t)SratTable + SratTable->Header.Length));
	}

	/* Enumerate SRAT */
	if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_SBST, 0, &Header)))
	{
		/* Variables */
		ACPI_TABLE_SBST *BattTable = NULL;

		/* Info */
		LogInformation("ACPI", "Parsing the SBST Table");

		/* Cast */
		BattTable = (ACPI_TABLE_SBST*)Header;

		/* Gogo */

	}

	/* Done! */
	return 0;
}

/* This returns 0 if ACPI is not available
 * on the system, or 1 if acpi is available */
int AcpiAvailable(void)
{
	return GlbAcpiAvailable;
}
