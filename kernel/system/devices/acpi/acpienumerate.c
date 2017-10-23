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
 * MollenOS MCore - ACPI(CA) Table Enumeration Interface
 */

/* Includes */
#include <arch.h>
#include <acpiinterface.h>
#include <heap.h>
#include <log.h>

/* C-Library */
#include <ds/list.h>

/* Globals 
 * - Global state keeping */
AcpiEcdt_t __GlbECDT;
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

/* Enumerate the ECDT 
 * We need this to fully initialize ACPI if it needs to be available. */
void
AcpiEnumerateECDT(
	_In_ void *TableStart,
	_In_ void *TableEnd)
{
	// Variables
	ACPI_TABLE_ECDT *EcdtTable = NULL;

	// Initialize pointers
	EcdtTable = (ACPI_TABLE_ECDT*)TableStart;

	// Store the most relevant data
	__GlbECDT.Handle = ACPI_ROOT_OBJECT;
	__GlbECDT.Gpe = EcdtTable->Gpe;
	__GlbECDT.UId = EcdtTable->Uid;
	memcpy(&__GlbECDT.CommandAddress, &EcdtTable->Control, sizeof(ACPI_GENERIC_ADDRESS));
	memcpy(&__GlbECDT.DataAddress, &EcdtTable->Data, sizeof(ACPI_GENERIC_ADDRESS));
	memcpy(&__GlbECDT.NsPath[0], &EcdtTable->Id[0], strlen((const char*	)&EcdtTable->Id[0]));
}

/* AcpiEnumerate
 * Initializes Early Access and enumerates the APIC */
int
AcpiEnumerate(void)
{
	// Variables
	ACPI_TABLE_HEADER *Header = NULL;
	ACPI_STATUS Status = 0;
	
	// Initialize variables
	memset(&__GlbECDT, 0, sizeof(AcpiEcdt_t));
    
    // Call
	LogInformation("ACPI", "Initializing ACPICA");
    Status = AcpiInitializeSubsystem();
    
	// Sanity 
	// If this fails there is no ACPI on the system
	if (ACPI_FAILURE(Status)) {
		LogFatal("ACPI", "Failed to initialize early ACPI access,"
            "probable no ACPI available (%u)", Status);
		GlbAcpiAvailable = ACPI_NOT_AVAILABLE;
		return -1;
	}

	// Do the early table enumeration
	Status = AcpiInitializeTables((ACPI_TABLE_DESC*)&TableArray, ACPI_MAX_INIT_TABLES, TRUE);
	if (ACPI_FAILURE(Status)) {
		LogFatal("ACPI", "Failed to initialize early ACPI access,"
			"probable no ACPI available (%u)", Status);
		GlbAcpiAvailable = ACPI_NOT_AVAILABLE;
		return -1;
	}
	else {
		GlbAcpiAvailable = ACPI_AVAILABLE;
    }

	// Allocate ACPI node list
	GlbAcpiNodes = ListCreate(KeyInteger, LIST_NORMAL);

	// Check for MADT presence and enumerate
	if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_MADT, 0, &Header))) {
		ACPI_TABLE_MADT *MadtTable = NULL;
		LogInformation("ACPI", "Enumerating the MADT Table");
		MadtTable = (ACPI_TABLE_MADT*)Header;
		AcpiEnumarateMADT((void*)((uintptr_t)MadtTable + sizeof(ACPI_TABLE_MADT)),
            (void*)((uintptr_t)MadtTable + MadtTable->Header.Length));

        // Cleanup table when we are done with it as we are using
        // static pointers and reallocating later
        AcpiPutTable(Header);
	}

	// Check for SRAT presence and enumerate
	if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_SRAT, 0, &Header))) {
		ACPI_TABLE_SRAT *SratTable = NULL;
		LogInformation("ACPI", "Enumerating the SRAT Table");
		SratTable = (ACPI_TABLE_SRAT*)Header;
		AcpiEnumerateSRAT((void*)((uintptr_t)SratTable + sizeof(ACPI_TABLE_SRAT)),
            (void*)((uintptr_t)SratTable + SratTable->Header.Length));
        
        // Cleanup table when we are done with it as we are using
        // static pointers and reaollcating later
        AcpiPutTable(Header);
	}

	// Check for SRAT presence and enumerate
	if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_ECDT, 0, &Header))) {
		ACPI_TABLE_ECDT *EcdtTable = NULL;
		LogInformation("ACPI", "Enumerating the ECDT Table");
		EcdtTable = (ACPI_TABLE_ECDT*)Header;
		//AcpiEnumerateECDT((void*)((uintptr_t)EcdtTable + sizeof(ACPI_TABLE_ECDT)),
        //    (void*)((uintptr_t)EcdtTable + EcdtTable->Header.Length));
        
        // Cleanup table when we are done with it as we are using
        // static pointers and reaollcating later
        AcpiPutTable(Header);
	}

    // Check for SBST presence and enumerate
    // @todo
	if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_SBST, 0, &Header))) {
		ACPI_TABLE_SBST *BattTable = NULL;
		LogInformation("ACPI", "Parsing the SBST Table");
        BattTable = (ACPI_TABLE_SBST*)Header;
        
        // Cleanup table when we are done with it as we are using
        // static pointers and reaollcating later
        AcpiPutTable(Header);
	}
	return 0;
}

/* This returns 0 if ACPI is not available
 * on the system, or 1 if acpi is available */
int AcpiAvailable(void)
{
	return GlbAcpiAvailable;
}
