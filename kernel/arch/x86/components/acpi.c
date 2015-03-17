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

#include <Arch.h>
#include <assert.h>
#include <acpi.h>
#include <stdio.h>
#include <Heap.h>
#include <List.h>

/* OSC */
#define ACPI_OSC_QUERY_INDEX				0
#define ACPI_OSC_SUPPORT_INDEX				1
#define ACPI_OSC_CONTROL_INDEX				2

#define ACPI_OSC_QUERY_ENABLE				0x1

#define ACPI_OSC_SUPPORT_SB_PR3_SUPPORT		0x4
#define ACPI_OSC_SUPPORT_SB_APEI_SUPPORT	0x10

struct _acpi_osc
{
	/* Object UUID */
	char *uuid;

	/* Revision = 1 */
	int revision;

	/* list of DWORD capabilities */
	ACPI_BUFFER capabilities;

	/* free by caller if success */
	ACPI_BUFFER retval;
};

/* ACPICA Stuff */
#define ACPI_MAX_INIT_TABLES 16
static ACPI_TABLE_DESC TableArray[ACPI_MAX_INIT_TABLES];
extern void AcpiUtConvertStringToUuid(char*, UINT8*);

/* Global ACPI Information */
char *sb_uuid_str = "0811B06E-4A27-44F9-8D60-3CBBC22E7B48";
char *osc_uuid_str = "33DB4D5B-1FF7-401C-9657-7441C03DD766";
list_t *acpi_nodes = NULL;
volatile Addr_t local_apic_addr = 0;
volatile uint32_t num_cpus = 0;

/* Fixed Event Handlers */
UINT32 AcpiShutdownHandler(void *Context)
{
	ACPI_EVENT_STATUS eStatus;
	ACPI_STATUS Status;

	/* Get Event Data */
	Status = AcpiGetEventStatus(ACPI_EVENT_POWER_BUTTON, &eStatus);

	/* Sanity */
	assert(ACPI_SUCCESS(Status));

	/* */
	if (eStatus & ACPI_EVENT_FLAG_ENABLED)
	{
		/* bla bla */
		AcpiClearEvent(ACPI_EVENT_POWER_BUTTON);
	}

	/* Shutdown - State S5 */
	AcpiEnterSleepState(ACPI_STATE_S5);

	return AE_OK;
}

UINT32 AcpiSleepHandler(void *Context)
{



	//AcpiEnterSleepState
	return AE_OK;
}

UINT32 AcpiRebootHandler(void)
{
	ACPI_STATUS status = AcpiReset();

	if (ACPI_FAILURE(status))
		printf("Reboot is unsupported\n");
	else
		printf("Reboot is in progress...\n");

	/* Safety Catch */
	for (;;);
}

/* Notify Handler */
void AcpiBusNotifyHandler(ACPI_HANDLE Device, UINT32 NotifyType, void *Context)
{
	_CRT_UNUSED(Device);
	_CRT_UNUSED(Context);

	printf("ACPI_Notify: Type 0x%x\n", NotifyType);
}

/* Global Event Handler */
void AcpiEventHandler(UINT32 EventType, ACPI_HANDLE Device, UINT32 EventNumber, void *Context)
{
	_CRT_UNUSED(Device);
	_CRT_UNUSED(Context);

	printf("ACPI_Event: Type 0x%x, Number 0x%x\n", EventType, EventNumber);
}

/* Interface Handlers */
UINT32 AcpiOsi(ACPI_STRING InterfaceName, UINT32 Supported)
{
	if (InterfaceName != NULL)
		return Supported;

	return Supported;
}

/* Run OSC Query */
ACPI_STATUS AcpiRunOscRequest(ACPI_HANDLE device, struct _acpi_osc *osc)
{
	ACPI_STATUS status = AE_ERROR;
	ACPI_OBJECT_LIST input;
	ACPI_OBJECT in_params[4];
	ACPI_OBJECT *out_param;
	uint8_t uuid[16];
	ACPI_BUFFER output;
	uint32_t query_status = 0;

	/* Sanity */
	if(osc == NULL)
		return AE_BAD_DATA;

	/* Convert */
	AcpiUtConvertStringToUuid(osc->uuid, uuid);

	/* Setup return object */
	osc->retval.Length = ACPI_ALLOCATE_BUFFER;
	osc->retval.Pointer = NULL;

	/* Setup output */
	output.Length = ACPI_ALLOCATE_BUFFER;
	output.Pointer = NULL;

	/* Set up list */
	input.Count = 4;
	input.Pointer = in_params;

	/* Set up parameters */
	in_params[0].Type = ACPI_TYPE_BUFFER;
	in_params[0].Buffer.Length = sizeof(uuid);
	in_params[0].Buffer.Pointer = uuid;

	in_params[1].Type = ACPI_TYPE_INTEGER;
	in_params[1].Integer.Value = osc->revision;

	in_params[2].Type = ACPI_TYPE_INTEGER;
	in_params[2].Integer.Value = (osc->capabilities.Length / sizeof(ACPI_SIZE));

	in_params[3].Type = ACPI_TYPE_BUFFER;
	in_params[3].Buffer.Length = osc->capabilities.Length;
	in_params[3].Buffer.Pointer = osc->capabilities.Pointer;

	/* Evaluate Object */
	status = AcpiEvaluateObject(device, "_OSC", &input, &output);

	/* Sanity */
	if (ACPI_FAILURE(status))
		return status;

	/* More Sanity */
	if (!output.Length)
		return AE_NULL_OBJECT;

	out_param = output.Pointer;

	/* Sanity */
	if (out_param->Type != ACPI_TYPE_BUFFER
		|| out_param->Buffer.Length != osc->capabilities.Length)
	{
		/* OSC Returned wrong type, sounds wierd */
		status = AE_TYPE;
		goto fail;
	}

	/* Now check error codes in query dword (Ignore bit 0) */
	query_status = *((uint32_t*)out_param->Buffer.Pointer);
	query_status &= ~(ACPI_OSC_QUERY_ENABLE);

	if (query_status)
	{
		printf("OSC Query Failed, Status Word: 0x%x\n", query_status);
		status = AE_ERROR;
		goto fail;
	}

	/* Set return object */
	osc->retval.Length = out_param->Buffer.Length;
	osc->retval.Pointer = out_param->Buffer.Pointer;

	/* Allocate a new buffer */
	osc->retval.Pointer = kmalloc(out_param->Buffer.Length);

	/* Sanity */
	if (osc->retval.Pointer == NULL)
	{
		status = AE_NO_MEMORY;
		goto fail;
	}
	else
		memcpy(osc->retval.Pointer, out_param->Buffer.Pointer, out_param->Buffer.Length);

	/* Set ok */
	status = AE_OK;

fail:
	/* Free */
	kfree(output.Pointer);

	/* Return */
	if (status != AE_OK)
		osc->retval.Pointer = NULL;
	
	return status;
}

/* Run OSC Support */
void AcpiCheckBusOscSupport(void)
{
	/* Decls */
	ACPI_HANDLE handle;
	uint32_t capabilities[2];
	struct _acpi_osc osc;
	uint32_t apei_support;

	/* Setup OSC */
	osc.uuid = sb_uuid_str;
	osc.revision = 1;
	osc.capabilities.Length = sizeof(capabilities);
	osc.capabilities.Pointer = capabilities;

	/* Setup capabilities */
	capabilities[ACPI_OSC_QUERY_INDEX] = ACPI_OSC_QUERY_ENABLE;
	capabilities[ACPI_OSC_SUPPORT_INDEX] = ACPI_OSC_SUPPORT_SB_PR3_SUPPORT;

	/* Haha we have like zero power management support, gg wp */

	/* Get root handle */
	if (ACPI_FAILURE(AcpiGetHandle(NULL, "\\_SB", &handle)))
		return;

	/* Run OSC Query */
	if (ACPI_SUCCESS(AcpiRunOscRequest(handle, &osc)))
	{
		/* Get capabilities */
		uint32_t *capabilitybuffer = osc.retval.Pointer;

		/* Did we get more than we asked for? */
		if (osc.retval.Length > ACPI_OSC_SUPPORT_INDEX)
			apei_support = capabilitybuffer[ACPI_OSC_SUPPORT_INDEX] & ACPI_OSC_SUPPORT_SB_APEI_SUPPORT;

		/* Cleanup */
		kfree(osc.retval.Pointer);
	}
}

/* Enumerate MADT Entries */
void AcpiEnumarateMADT(void *start, void *end)
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
void AcpiInitStage1(void)
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
	if (!MmVirtualGetMapping(NULL, local_apic_addr))
		MmVirtualMap(NULL, local_apic_addr, local_apic_addr, 0);

	/* Enumerate MADT */
	AcpiEnumarateMADT((void*)((Addr_t)madt + sizeof(ACPI_TABLE_MADT)), (void*)((Addr_t)madt + madt->Header.Length));

	/* Enumerate SRAT */

	/* Enumerate ECDT */
}

/* Initializes FULL access 
 * across ACPICA */
void AcpiInitStage2(void)
{
	ACPI_STATUS Status;
	ACPI_OBJECT arg1;
	ACPI_OBJECT_LIST args;

	/* Initialize the ACPICA subsystem */
	printf("  - Acpica Stage 2 Starting\n");
	printf("    * Initializing subsystems\n");
	Status = AcpiInitializeSubsystem();
	if (ACPI_FAILURE(Status))
	{
		printf("    * FAILED InititalizeSubsystem, %u!\n", Status);
		for (;;);
	}

	/* Copy the root table list to dynamic memory */
	printf("    * Reallocating tables\n");
	Status = AcpiReallocateRootTable();
	if (ACPI_FAILURE(Status))
	{
		/*  */
		printf("    * FAILED AcpiReallocateRootTable, %u!\n", Status);
		for (;;);
	}

	/* Install the default address space handlers. */
	Status = AcpiInstallAddressSpaceHandler(ACPI_ROOT_OBJECT,
		ACPI_ADR_SPACE_SYSTEM_MEMORY, ACPI_DEFAULT_HANDLER, NULL, NULL);
	if (ACPI_FAILURE(Status)) {
		printf("Could not initialise SystemMemory handler: %s\n",
			AcpiFormatException(Status));
	}

	Status = AcpiInstallAddressSpaceHandler(ACPI_ROOT_OBJECT,
		ACPI_ADR_SPACE_SYSTEM_IO, ACPI_DEFAULT_HANDLER, NULL, NULL);
	if (ACPI_FAILURE(Status)) {
		printf("Could not initialise SystemIO handler: %s\n",
			AcpiFormatException(Status));
	}

	Status = AcpiInstallAddressSpaceHandler(ACPI_ROOT_OBJECT,
		ACPI_ADR_SPACE_PCI_CONFIG, ACPI_DEFAULT_HANDLER, NULL, NULL);
	if (ACPI_FAILURE(Status)) {
		printf("Could not initialise PciConfig handler: %s\n",
			AcpiFormatException(Status));
	}

	/* Create the ACPI namespace from ACPI tables */
	printf("    * Loading tables\n");
	Status = AcpiLoadTables();
	if (ACPI_FAILURE(Status))
	{
		printf("    * FAILED LoadTables, %u!\n", Status);
		for (;;);
	}

	/* Install OSL Handler */
	AcpiInstallInterfaceHandler(AcpiOsi);

	/* Initialize the ACPI hardware */
	printf("    * Enabling subsystems\n");
	Status = AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);
	if (ACPI_FAILURE(Status))
	{
		printf("    * FAILED EnableSystem, %u!\n", Status);
	}
	
	/* Probe for EC here */

	/* Complete the ACPI namespace object initialization */
	printf("    * Initializing objects\n");
	Status = AcpiInitializeObjects(ACPI_FULL_INITIALIZATION);
	if (ACPI_FAILURE(Status))
	{
		printf("    * FAILED InitializeObjects, %u!\n", Status);
		for (;;);
	}

	/* Run _OSC on root, it should always be run after InitializeObjects */
	AcpiCheckBusOscSupport();

	/* Set APIC Mode */
	arg1.Type = ACPI_TYPE_INTEGER;
	arg1.Integer.Value = 1;				/* 0 - PIC, 1 - IOAPIC, 2 - IOSAPIC */
	args.Count = 1;
	args.Pointer = &arg1;

	AcpiEvaluateObject(ACPI_ROOT_OBJECT, "_PIC", &args, NULL);

	/* Install a notify handler */
	AcpiInstallNotifyHandler(ACPI_ROOT_OBJECT, ACPI_SYSTEM_NOTIFY, AcpiBusNotifyHandler, NULL);

	/* Install a global event handler */
	AcpiInstallGlobalEventHandler(AcpiEventHandler, NULL);
	//AcpiInstallFixedEventHandler(ACPI_EVENT_POWER_BUTTON, acpi_shutdown, NULL);
	//AcpiInstallFixedEventHandler(ACPI_EVENT_SLEEP_BUTTON, acpi_sleep, NULL);

	printf("    * Acpica Stage 2 Started\n");
}