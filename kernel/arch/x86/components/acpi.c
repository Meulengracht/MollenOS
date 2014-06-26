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
* MollenOS x86-32 ACPI Interface (Uses ACPICA)
*/

#include <arch.h>
#include <acpi.h>
#include <stdio.h>
#include <heap.h>
#include <list.h>

/* ACPICA Stuff */
#define ACPI_MAX_INIT_TABLES 16
static ACPI_TABLE_DESC TableArray[ACPI_MAX_INIT_TABLES];

/* Global ACPI Information */
list_t *acpi_nodes = NULL;
volatile addr_t local_apic_addr = 0;
volatile uint32_t num_cpus = 0;

/* Enumerate MADT Entries */
void madt_enumerate(void *start, void *end)
{
	ACPI_SUBTABLE_HEADER *entry;

	for (entry = (ACPI_SUBTABLE_HEADER*)start; (void *)entry < end;)
	{
		/* Avoid an infinite loop if we hit a bogus entry. */
		if (entry->Length < sizeof(ACPI_SUBTABLE_HEADER))
			return;

		switch (entry->Type)
		{
			/* Processor Core */
		case ACPI_MADT_TYPE_LOCAL_APIC:
		{
			/* Cast to correct structure */
			ACPI_MADT_LOCAL_APIC *cpu_node = (ACPI_MADT_LOCAL_APIC*)kmalloc(sizeof(ACPI_MADT_LOCAL_APIC));
			ACPI_MADT_LOCAL_APIC *cpu = (ACPI_MADT_LOCAL_APIC*)entry;

			/* Now we have it allocated aswell, copy info */
			memcpy(cpu_node, cpu, sizeof(ACPI_MADT_LOCAL_APIC));

			/* Insert it into list */
			list_append(acpi_nodes, list_create_node(ACPI_MADT_TYPE_LOCAL_APIC, cpu_node));

			printf("      > Found CPU: %u (Flags 0x%x)\n", cpu->Id, cpu->LapicFlags);

			/* Increase CPU count */
			num_cpus++;

		} break;

		/* IO Apic */
		case ACPI_MADT_TYPE_IO_APIC:
		{
			/* Cast to correct structure */
			ACPI_MADT_IO_APIC *apic_node = (ACPI_MADT_IO_APIC*)kmalloc(sizeof(ACPI_MADT_IO_APIC));
			ACPI_MADT_IO_APIC *apic = (ACPI_MADT_IO_APIC*)entry;

			/* Now we have it allocated aswell, copy info */
			memcpy(apic_node, apic, sizeof(ACPI_MADT_IO_APIC));

			/* Insert it into list */
			list_append(acpi_nodes, list_create_node(ACPI_MADT_TYPE_IO_APIC, apic_node));

			printf("      > Found IO-APIC: %u\n", apic->Id);

		} break;

		/* Interrupt Overrides */
		case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE:
		{
			/* Cast to correct structure */
			ACPI_MADT_INTERRUPT_OVERRIDE *io_node = (ACPI_MADT_INTERRUPT_OVERRIDE*)kmalloc(sizeof(ACPI_MADT_INTERRUPT_OVERRIDE));
			ACPI_MADT_INTERRUPT_OVERRIDE *io_override = (ACPI_MADT_INTERRUPT_OVERRIDE*)entry;

			/* Now we have it allocated aswell, copy info */
			memcpy(io_node, io_override, sizeof(ACPI_MADT_INTERRUPT_OVERRIDE));

			/* Insert it into list */
			list_append(acpi_nodes, list_create_node(ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, io_node));

			printf("      > Found Interrupt Override: %u -> %u\n", io_override->SourceIrq, io_override->GlobalIrq);

		} break;

		/* Local APIC NMI Configuration */
		case ACPI_MADT_TYPE_LOCAL_APIC_NMI:
		{
			/* Cast to correct structure */
			ACPI_MADT_LOCAL_APIC_NMI *nmi_apic = (ACPI_MADT_LOCAL_APIC_NMI*)kmalloc(sizeof(ACPI_MADT_LOCAL_APIC_NMI));
			ACPI_MADT_LOCAL_APIC_NMI *nmi_override = (ACPI_MADT_LOCAL_APIC_NMI*)entry;

			/* Now we have it allocated aswell, copy info */
			memcpy(nmi_apic, nmi_override, sizeof(ACPI_MADT_LOCAL_APIC_NMI));

			/* Insert it into list */
			list_append(acpi_nodes, list_create_node(ACPI_MADT_TYPE_LOCAL_APIC_NMI, nmi_apic));

			printf("      > Found Local APIC NMI: LintN %u connected to CPU %u\n", nmi_override->Lint, nmi_override->ProcessorId);

			/* Install */
			if (nmi_override->ProcessorId == 0xFF)
			{
				/* Broadcast */
			}
			else
			{
				/* Set core LVT */
			}

		} break;

		default:
			printf("      > Found Type %u\n", entry->Type);
			break;
		}

		/* Next */
		entry = (ACPI_SUBTABLE_HEADER*)ACPI_ADD_PTR(ACPI_SUBTABLE_HEADER, entry, entry->Length);
	}
}

/* Initializes Early Access 
 * and enumerates the APIC */
void acpi_init_stage1(void)
{
	/* Vars */
	ACPI_TABLE_MADT *madt = NULL;
	ACPI_TABLE_HEADER *header = NULL;
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
	printf("    * Acpica Stage 1 Started\n");

	/* Get the table */
	if (ACPI_FAILURE(AcpiGetTable(ACPI_SIG_MADT, 0, &header)))
	{
		/* Damn :( */
		printf("    * APIC / ACPI FAILURE, APIC TABLE DOES NOT EXIST!\n");

		/* Stall */
		for (;;);
	}

	/* Cast */
	madt = (ACPI_TABLE_MADT*)header;
	
	/* Create the list */
	acpi_nodes = list_create(LIST_NORMAL);

	/* Get Local Apic Address */
	local_apic_addr = madt->Address;
	num_cpus = 0;

	/* Identity map it in */
	if (!memory_getmap(NULL, local_apic_addr))
		memory_map(NULL, local_apic_addr, local_apic_addr, 0);

	/* Enumerate MADT */
	madt_enumerate((void*)((addr_t)madt + sizeof(ACPI_TABLE_MADT)), (void*)((addr_t)madt + madt->Header.Length));

	/* Enumerate SRAT */

	/* Enumerate ECDT */
}

/* Initializes FULL access 
 * across ACPICA */
void acpi_init_stage2(void)
{
	ACPI_STATUS Status;

	/* Initialize the ACPICA subsystem */
	printf("    * Acpica Stage 2 Starting\n");
	printf("      > Initializing subsystems\n");
	Status = AcpiInitializeSubsystem();
	if (ACPI_FAILURE(Status))
	{
		printf("      > FAILED InititalizeSubsystem, %u!\n", Status);
		for (;;);
	}

	/* Copy the root table list to dynamic memory */
	printf("      > Reallocating tables\n");
	Status = AcpiReallocateRootTable();
	if (ACPI_FAILURE(Status))
	{
		/*  */
		printf("      > FAILED AcpiReallocateRootTable, %u!\n", Status);
		for (;;);
	}

	/* Create the ACPI namespace from ACPI tables */
	printf("      > Loading tables\n");
	Status = AcpiLoadTables();
	if (ACPI_FAILURE(Status))
	{
		printf("      > FAILED LoadTables, %u!\n", Status);
		for (;;);
	}
	
	
	/* Note: Local handlers should be installed here */


	/* Initialize the ACPI hardware */
	printf("      > Enabling subsystems\n");
	Status = AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);
	if (ACPI_FAILURE(Status))
	{
		printf("      > FAILED EnableSystem, %u!\n", Status);
		for (;;);
	}
	
	/* Complete the ACPI namespace object initialization */
	printf("      > Initializing objects\n");
	Status = AcpiInitializeObjects(ACPI_FULL_INITIALIZATION);
	if (ACPI_FAILURE(Status))
	{
		printf("      > FAILED InitializeObjects, %u!\n", Status);
		for (;;);
	}

	/* Now it is expected to do namespace walk and execute
	 * all _PRW methods. I should install GPE handlers here */

	/* Call this on all _PRW methods, AcpiSetWakeGpe */
	
	//AcpiInstallGpeHandler()
	//AcpiInstallGlobalEventHandler()
	/* Call this when done */
	//AcpiUpdateAllGpes();

	printf("    * Acpica Stage 2 Started\n");
}