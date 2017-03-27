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
#include <AcpiInterface.h>
#include <Heap.h>
#include <Log.h>

/* C-Library */
#include <stddef.h>
#include <ds/list.h>

/* Globals */
List_t *GlbPciAcpiDevices = NULL;
int GlbBusCounter = 0;

/* Lookup a bridge device for the given
 * bus that contains pci routings */
AcpiDevice_t *AcpiLookupDevice(int Bus) 
{
	// Variables
	AcpiDevice_t *Dev = NULL;
	DataKey_t Key;
	int Index = 0;

	// Loop through buses
	while (1) {
		Key.Value = ACPI_BUS_ROOT_BRIDGE;
		Dev = (AcpiDevice_t*)ListGetDataByKey(GlbPciAcpiDevices, Key, Index);

		// Sanity, if this returns
		// null we are out of data
		if (Dev == NULL) {
			break;
		}

		// Match the bus 
		if (Dev->Bus == Bus && Dev->Routings != NULL) {
			return Dev;
		}

		// Next bus
		Index++;
	}

	// Not found
	return NULL;
}

/* Gathers information about a ACPICA handle
 * stores it into AcpiDevice structure for later
 * use so we never have to query acpica again, for
 * performance. We also do ACPI setup for the device
 * in this function */
AcpiDevice_t *PciAddObject(ACPI_HANDLE Handle, ACPI_HANDLE Parent, uint32_t Type)
{
	ACPI_BUFFER Buffer;
	ACPI_STATUS Status;
	AcpiDevice_t *Device;
	DataKey_t Key;

	/* Allocate Resources */
	Device = (AcpiDevice_t*)kmalloc(sizeof(AcpiDevice_t));

	/* Memset */
	memset(Device, 0, sizeof(AcpiDevice_t));

	/* Allocate name resources */
	Device->Name = (char*)kmalloc(128);
	memset(Device->Name, 0, 128);

	/* Set handle */
	Device->Handle = Handle;

	/* Get Bus Identifier */
	Status = AcpiDeviceGetBusId(Device, Type);

	/* Which namespace functions is supported? */
	Status = AcpiDeviceGetFeatures(Device);

	/* Get Bus and Seg Number */
	Status = AcpiDeviceGetBusAndSegment(Device);

	/* Retrieve the name of the acpi device */
	Buffer.Length = 128;
	Buffer.Pointer = Device->Name;
	Status = AcpiGetName(Handle, ACPI_FULL_PATHNAME, &Buffer);

	/* Sanity */
	if (ACPI_FAILURE(Status)) {
		memset(Device->Name, 0, 128);
		strcpy(Device->Name, "(null)");
	}

	/* Check device status */
	switch (Type)
	{
		/* Same handling for these */
		case ACPI_BUS_TYPE_DEVICE:
		case ACPI_BUS_TYPE_PROCESSOR:
		{
			/* Get Status */
			Status = AcpiDeviceGetStatus(Device);

			if (ACPI_FAILURE(Status))
			{
				LogDebug("ACPI", "Device %s failed its dynamic status check", Device->BusId);
				kfree(Device);
				return NULL;
			}

			/* Is it present and functioning? */
			if (!(Device->Status & ACPI_STA_DEVICE_PRESENT) &&
				!(Device->Status & ACPI_STA_DEVICE_FUNCTIONING))
			{
				LogDebug("ACPI", "Device %s is not present or functioning", Device->BusId);
				kfree(Device);
				return NULL;
			}
		}

		default:
			Device->Status = ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED |
								ACPI_STA_DEVICE_UI | ACPI_STA_DEVICE_FUNCTIONING;
	}

	/* Now, get HID, ADDR and UUID */
	Status = AcpiDeviceGetHWInfo(Device, Parent, Type);
	
	/* Make sure this call worked */
	if (ACPI_FAILURE(Status))
	{
		LogDebug("ACPI", "Failed to retrieve object information about device %s", Device->BusId);
		kfree(Device);
		return NULL;
	}

	/* Store the device structure with the object itself */
	Status = AcpiDeviceAttachData(Device, Type);
	
	/* Convert ADR to device / function */
	if (Device->Features & ACPI_FEATURE_ADR)
	{
		Device->Device = ACPI_HIWORD(ACPI_LODWORD(Device->Address));
		Device->Function = ACPI_LOWORD(ACPI_LODWORD(Device->Address));

		/* Sanity Values */
		if (Device->Device > 31)
			Device->Device = 0;
		if (Device->Function > 8)
			Device->Function = 0;
	}
	else
	{
		Device->Device = 0;
		Device->Function = 0;
	}

	/* Does it contain routings */
	if (Device->Features & ACPI_FEATURE_PRT) {
		Status = AcpiDeviceGetIrqRoutings(Device);
		if (ACPI_FAILURE(Status))
			LogDebug("ACPI", "Failed to retrieve pci irq routings from device %s (%u)", Device->BusId, Status);
	}

	// EC: PNP0C09
	// EC Batt: PNP0C0A
	// Smart Battery Ctrl HID: ACPI0001
	// Smart Battery HID: ACPI0002

	/* Is this root bus? */
	if (strncmp(Device->HID, "PNP0A03", 7) == 0 ||
		strncmp(Device->HID, "PNP0A08", 7) == 0)	/* PCI or PCI-express */
	{
		/* First, we have to negiotiate OS Control */
		//pci_negiotiate_os_control(device);

		/* Save it root bridge list */
		Device->Type = ACPI_BUS_ROOT_BRIDGE;
		Device->Bus = GlbBusCounter;
		GlbBusCounter++;

		/* Perform PCI Config Space Initialization */
		AcpiInstallAddressSpaceHandler(Device->Handle, ACPI_ADR_SPACE_PCI_CONFIG, ACPI_DEFAULT_HANDLER, NULL, NULL);
	}
	else
		Device->Type = Type;

	/* Add to list and return */
	Key.Value = Device->Type;
	ListAppend(GlbPciAcpiDevices, ListCreateNode(Key, Key, Device));

	return Device;
}

/* Scan callback from the AcpiGetDevices
 * everytime we are called it's because
 * the acpica system has discovered a new
 * device on the bus (handle) */
ACPI_STATUS PciScanCallback(ACPI_HANDLE Handle, UINT32 Level, void *Context, void **ReturnValue)
{
	/* Variables */
	AcpiDevice_t *Device = NULL;
	ACPI_STATUS Status = AE_OK;
	ACPI_OBJECT_TYPE Type = 0;
	ACPI_HANDLE Parent = (ACPI_HANDLE)Context;

	/* Get Type */
	Status = AcpiGetType(Handle, &Type);

	if (ACPI_FAILURE(Status))
		return AE_OK;

	/* We dont want ALL types obviously */
	switch (Type)
	{
		case ACPI_TYPE_DEVICE:
			Type = ACPI_BUS_TYPE_DEVICE;
			break;
		case ACPI_TYPE_PROCESSOR:
			Type = ACPI_BUS_TYPE_PROCESSOR;
			break;
		case ACPI_TYPE_THERMAL:
			Type = ACPI_BUS_TYPE_THERMAL;
			break;
		case ACPI_TYPE_POWER:
			Type = ACPI_BUS_TYPE_PWM;
		default:
			return AE_OK;
	}

	/* Get Parent */
	Status = AcpiGetParent(Handle, &Parent);

	/* Add object to list */
	Device = PciAddObject(Handle, Parent, Type);
	
	/* Sanity */
	if (Device != NULL) {
		//acpi_scan_init_hotplug(device);
	}

	return AE_OK;
}

/* Scan the ACPI namespace for devices
 * and irq-routings, this is very neccessary
 * for getting correct irqs */
void AcpiScan(void)
{
	/* Log */
	LogInformation("ACPI", "Installing Fixables");

	/* Init list, this is "bus 0" */
	GlbPciAcpiDevices = ListCreate(KeyInteger, LIST_SAFE);

	/* Step 1. Enumerate Fixed Objects */
	if (AcpiGbl_FADT.Flags & ACPI_FADT_POWER_BUTTON)
		PciAddObject(NULL, ACPI_ROOT_OBJECT, ACPI_BUS_TYPE_POWER);

	if (AcpiGbl_FADT.Flags & ACPI_FADT_SLEEP_BUTTON)
		PciAddObject(NULL, ACPI_ROOT_OBJECT, ACPI_BUS_TYPE_SLEEP);

	/* Log */
	LogInformation("ACPI", "Scanning Bus");
	
	/* Step 2. Enumerate */
	AcpiGetDevices(NULL, PciScanCallback, NULL, NULL);
}