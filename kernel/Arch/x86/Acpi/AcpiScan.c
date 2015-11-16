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
#include <acpi.h>
#include <List.h>

/* Globals */
list_t *GlbPciAcpiDevices = NULL;

/* Get Irq by Bus / Dev / Pin 
 * Returns -1 if no overrides exists */
int AcpiDeviceGetIrq(uint32_t Bus, uint32_t Device, uint32_t Pin, 
					uint8_t *TriggerMode, uint8_t *Polarity, uint8_t *Shareable,
					uint8_t *Fixed)
{
	/* Locate correct bus */
	int n = 0;
	PciAcpiDevice_t *Dev;
	
	while (1)
	{
		Dev = (PciAcpiDevice_t*)list_get_data_by_id(GlbPciAcpiDevices, ACPI_BUS_ROOT_BRIDGE, n);

		if (Dev == NULL)
			break;

		/* Todo, make sure we find the correct root-bridge */
		if (Dev->Routings != NULL)
		{
			/* Get offset */
			uint32_t toffset = (Device * 4) + Pin;

			/* Update IRQ Information */
			if (Dev->Routings->Trigger[toffset] == ACPI_LEVEL_SENSITIVE)
				*TriggerMode = 1;
			else
				*TriggerMode = 0;

			if (Dev->Routings->Polarity[toffset] == ACPI_ACTIVE_LOW)
				*Polarity = 1;
			else
				*Polarity = 0;

			*Shareable = Dev->Routings->Shareable[toffset];
			*Fixed = Dev->Routings->Fixed[toffset];
			return Dev->Routings->Interrupts[toffset];
		}
		
		/* Increase N */
		n++;
	}

	return -1;
}

/* Adds an object to the Acpi List */
PciAcpiDevice_t *PciAddObject(ACPI_HANDLE Handle, ACPI_HANDLE Parent, uint32_t Type)
{
	ACPI_STATUS Status;
	PciAcpiDevice_t *Device;

	/* Allocate Resources */
	Device = (PciAcpiDevice_t*)kmalloc(sizeof(PciAcpiDevice_t));

	/* Memset */
	memset(Device, 0, sizeof(PciAcpiDevice_t));

	/* Set handle */
	Device->Handle = Handle;

	/* Get Bus Identifier */
	Status = AcpiDeviceGetBusId(Device, Type);

	/* Which namespace functions is supported? */
	Status = AcpiDeviceGetFeatures(Device);

	/* Get Bus and Seg Number */
	Status = AcpiDeviceGetBusAndSegment(Device);

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
				printf("ACPI: Device %s failed its dynamic status check\n", Device->BusId);
				kfree(Device);
				return NULL;
			}

			/* Is it present and functioning? */
			if (!(Device->Status & ACPI_STA_DEVICE_PRESENT) &&
				!(Device->Status & ACPI_STA_DEVICE_FUNCTIONING))
			{
				printf("ACPI: Device %s is not present or functioning\n", Device->BusId);
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
		printf("ACPI: Failed to retrieve object information about device %s\n", Device->BusId);
		kfree(Device);
		return NULL;
	}

	/* Store the device structure with the object itself */
	Status = AcpiDeviceAttachData(Device, Type);
	
	/* Convert ADR to device / function */
	if (Device->Features & X86_ACPI_FEATURE_ADR)
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

	/* Here we would handle all kinds of shizzle */
	/* printf("[%u:%u:%u]: %s (Name %s, Flags 0x%x)\n", device->bus,
		device->dev, device->func, device->hid, device->bus_id, device->features); */

	/* Does it contain routings */
	if (Device->Features & X86_ACPI_FEATURE_PRT)
	{
		Status = AcpiDeviceGetIrqRoutings(Device);

		if (ACPI_FAILURE(Status))
			printf("ACPI: Failed to retrieve pci irq routings from device %s (%u)\n", Device->BusId, Status);
	}

	/* Is this root bus? */
	if (strncmp(Device->HID, "PNP0A03", 7) == 0 ||
		strncmp(Device->HID, "PNP0A08", 7) == 0)	/* PCI or PCI-express */
	{
		/* First, we have to negiotiate OS Control */
		//pci_negiotiate_os_control(device);

		/* OK so actually we can get the bus number from this, and then SIMPLY
		 * just use standard enumeration, wtf i did obviously fail at logic */
		PciCheckBus(GlbPciDevices, (uint8_t)Device->Bus);

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
	list_append(GlbPciAcpiDevices, list_create_node(Device->Type, Device));

	return Device;
}

/* Scan Callback */
ACPI_STATUS PciScanCallback(ACPI_HANDLE Handle, UINT32 Level, void *Context, void **ReturnValue)
{
	PciAcpiDevice_t *Device = NULL;
	ACPI_STATUS Status = AE_OK;
	ACPI_OBJECT_TYPE Type = 0;
	ACPI_HANDLE Parent = (ACPI_HANDLE)Context;
//	uint8_t x_name[128];
//	ACPI_BUFFER n_buf;

	/* Have we already enumerated this device? */
	/* HINT, look at attached data */
//	memset(x_name, 0, sizeof(x_name));
//	n_buf.Length = sizeof(x_name);
//	n_buf.Pointer = x_name;

	/* Get name */
	//status = AcpiGetName(handle, ACPI_FULL_PATHNAME, &n_buf);
	
	//if (ACPI_SUCCESS(status))
	//	printf("ACPI: %s\n", &x_name);

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
	if (!Device)
	{
		//acpi_scan_init_hotplug(device);
	}

	return AE_OK;
}

/* Scan the Acpi Devices */
void AcpiScan(void)
{
	/* Init list, this is "bus 0" */
	GlbPciAcpiDevices = list_create(LIST_SAFE);

	/* Step 1. Enumerate Fixed Objects */
	if (AcpiGbl_FADT.Flags & ACPI_FADT_POWER_BUTTON)
		PciAddObject(NULL, ACPI_ROOT_OBJECT, ACPI_BUS_TYPE_POWER);

	if (AcpiGbl_FADT.Flags & ACPI_FADT_SLEEP_BUTTON)
		PciAddObject(NULL, ACPI_ROOT_OBJECT, ACPI_BUS_TYPE_SLEEP);

	/* Step 2. Enumerate */
	//status = AcpiWalkNamespace(ACPI_TYPE_ANY, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX, pci_scan_callback, NULL, handle, NULL);
	AcpiGetDevices(NULL, PciScanCallback, NULL, NULL);
}