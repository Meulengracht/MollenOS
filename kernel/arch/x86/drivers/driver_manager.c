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
* MollenOS X86-32 Driver Manager
* Version 1. PCI Support Only (No PCI Express)
*/

/* Includes */
#include <arch.h>
#include <assert.h>
#include <acpi.h>
#include <pci.h>
#include <list.h>
#include <heap.h>
#include <stddef.h>
#include <stdio.h>
#include <limits.h>

/* Drivers */
#include <drivers\usb\uhci\uhci.h>
#include <drivers\usb\ohci\ohci.h>
#include <drivers\usb\ehci\ehci.h>

/* Prototypes */
void pci_check_function(list_t *bridge, uint8_t bus, uint8_t device, uint8_t function);
void pci_check_device(list_t *bridge, uint8_t bus, uint8_t device);
void pci_check_bus(list_t *bridge, uint8_t bus);

pci_device_t *pci_add_object(ACPI_HANDLE handle, ACPI_HANDLE parent, uint32_t type);

/* Globals */
list_t *glb_pci_devices = NULL;
list_t *glb_pci_acpica = NULL;
volatile uint32_t glb_bus_counter = 0;

/* Extern */
extern void threading_debug(void);

/* Check a function */
/* For each function we create a 
 * pci_device and add it to the list */
void pci_check_function(list_t *bridge, uint8_t bus, uint8_t device, uint8_t function)
{
	uint8_t sec_bus;
	pci_device_header_t *pcs;
	pci_driver_t *pci_driver;

	/* Allocate */
	pcs = (pci_device_header_t*)kmalloc(sizeof(pci_device_header_t));
	pci_driver = (pci_driver_t*)kmalloc(sizeof(pci_driver_t));

	/* Get full information */
	pci_read_function(pcs, bus, device, function);

	/* Set information */
	pci_driver->header = pcs;
	pci_driver->bus = bus;
	pci_driver->device = device;
	pci_driver->function = function;
	pci_driver->children = NULL;

	/* Info 
	 * Ignore the spam of device_id 0x7a0 in VMWare*/
	if (pcs->device_id != 0x7a0)
	{
		printf("    * [%d:%d:%d][%d:%d:%d] Vendor 0x%x, Device 0x%x : %s\n",
			pcs->class_code, pcs->subclass, pcs->ProgIF,
			bus, device, function,
			pcs->vendor_id, pcs->device_id,
			pci_to_string(pcs->class_code, pcs->subclass, pcs->ProgIF));
	}

	/* Do some disabling */
	if ((pcs->class_code != 0x06) && (pcs->class_code != 0x03))
	{
		/* Disable Device untill further notice */
		pci_write_word((const uint16_t)bus, (const uint16_t)device, (const uint16_t)function, 0x04, 0x0400);
	}
	
	/* Add to list */
	if (pcs->class_code == 0x06 && pcs->subclass == 0x04)
	{
		pci_driver->type = X86_PCI_TYPE_BRIDGE;
		list_append(bridge, list_create_node(X86_PCI_TYPE_BRIDGE, pci_driver));
	}
	else
	{
		pci_driver->type = X86_PCI_TYPE_DEVICE;
		list_append(bridge, list_create_node(X86_PCI_TYPE_DEVICE, pci_driver));
	}

	/* Is this a secondary (PCI) bus */
	if ((pcs->class_code == 0x06) && (pcs->subclass == 0x04))
	{
		/* Uh oh, this dude has children */
		pci_driver->children = list_create(LIST_SAFE);

		sec_bus = pci_read_secondary_bus_number(bus, device, function);
		pci_check_bus(pci_driver->children, sec_bus);
	}
}

/* Check a device */
void pci_check_device(list_t *bridge, uint8_t bus, uint8_t device)
{
	uint8_t function = 0;
	uint16_t vendor_id = 0;
	uint8_t header_type = 0;

	/* Get vendor id */
	vendor_id = pci_read_vendor_id(bus, device, function);

	/* Sanity */
	if (vendor_id == 0xFFFF)
		return;

	/* Check function 0 */
	pci_check_function(bridge, bus, device, function);
	header_type = pci_read_header_type(bus, device, function);

	/* Multi-function or single? */
	if (header_type & 0x80) 
	{
		/* It is a multi-function device, so check remaining functions */
		for (function = 1; function < 8; function++) 
		{
			/* Only check if valid vendor */
			if (pci_read_vendor_id(bus, device, function) != 0xFFFF)
				pci_check_function(bridge, bus, device, function);
		}
	}
}

/* Check a bus */
void pci_check_bus(list_t *bridge, uint8_t bus)
{
	uint8_t device;

	for (device = 0; device < 32; device++)
		pci_check_device(bridge, bus, device);
}

/* First of all, devices exists on TWO different
 * busses. PCI and PCI Express. */
void drivers_enumerate(void)
{
	uint8_t function;
	uint8_t bus;
	uint8_t header_type;

	header_type = pci_read_header_type(0, 0, 0);
	
	if ((header_type & 0x80) == 0) 
	{
		/* Single PCI host controller */
		printf("    * Single Bus Present\n");
		pci_check_bus(glb_pci_devices, 0);
	}
	else 
	{
		/* Multiple PCI host controllers */
		printf("    * Multi Bus Present\n");
		for (function = 0; function < 8; function++) 
		{
			if (pci_read_vendor_id(0, 0, function) != 0xFFFF)
				break;

			/* Check bus */
			bus = function;
			pci_check_bus(glb_pci_devices, bus);
		}
	}
}

/* Video Backlight Capability Callback */
ACPI_STATUS pci_video_backlight_callback(ACPI_HANDLE handle, UINT32 level, void *context, void **return_value)
{
	ACPI_HANDLE null_handle = NULL;
	uint64_t *video_features = (uint64_t*)context;

	if (ACPI_SUCCESS(AcpiGetHandle(handle, "_BCM", &null_handle)) &&
		ACPI_SUCCESS(AcpiGetHandle(handle, "_BCL", &null_handle)))
	{
		/* Add */
		*video_features |= ACPI_VIDEO_BACKLIGHT;
		
		/* Check for Brightness */
		if (ACPI_SUCCESS(AcpiGetHandle(handle, "_BQC", &null_handle)))
			*video_features |= ACPI_VIDEO_BRIGHTNESS;
		
		/* We have backlight support, no need to scan further */
		return AE_CTRL_TERMINATE;
	}

	return AE_OK;
}

/* Is this a video device? 
 * If it is, we also retrieve capabilities */
ACPI_STATUS pci_device_is_video(pci_device_t *device)
{
	ACPI_HANDLE null_handle = NULL;
	uint64_t video_features = 0;

	/* Sanity */
	if (device == NULL)
		return AE_ABORT_METHOD;

	/* Does device support Video Switching */
	if (ACPI_SUCCESS(AcpiGetHandle(device->handle, "_DOD", &null_handle)) &&
		ACPI_SUCCESS(AcpiGetHandle(device->handle, "_DOS", &null_handle)))
		video_features |= ACPI_VIDEO_SWITCHING;

	/* Does device support Video Rom? */
	if (ACPI_SUCCESS(AcpiGetHandle(device->handle, "_ROM", &null_handle)))
		video_features |= ACPI_VIDEO_ROM;

	/* Does device support configurable video head? */
	if (ACPI_SUCCESS(AcpiGetHandle(device->handle, "_VPO", &null_handle)) &&
		ACPI_SUCCESS(AcpiGetHandle(device->handle, "_GPD", &null_handle)) &&
		ACPI_SUCCESS(AcpiGetHandle(device->handle, "_SPD", &null_handle)))
		video_features |= ACPI_VIDEO_POSTING;

	/* Only call this if it is a video device */
	if (video_features != 0)
	{
		AcpiWalkNamespace(ACPI_TYPE_DEVICE, device->handle, ACPI_UINT32_MAX, 
			pci_video_backlight_callback, NULL, &video_features, NULL);

		/* Update ONLY if video device */
		device->xfeatures = video_features;

		return AE_OK;
	}
	else 
		return AE_NOT_FOUND;
}

/* Is this a docking device? 
 * If it has a _DCK method, yes */
ACPI_STATUS pci_device_is_dock(pci_device_t *device)
{
	ACPI_HANDLE null_handle = NULL;

	/* Sanity */
	if (device == NULL)
		return AE_ABORT_METHOD;
	else
		return AcpiGetHandle(device->handle, "_DCK", &null_handle);
}

/* Is this a BAY (i.e cd-rom drive with a ejectable bay) 
 * We check several ACPI methods here */
ACPI_STATUS pci_device_is_bay(pci_device_t *device)
{
	ACPI_STATUS status;
	ACPI_HANDLE parent_handle = NULL;
	ACPI_HANDLE null_handle = NULL;

	/* Sanity, make sure it is even ejectable */
	status = AcpiGetHandle(device->handle, "_EJ0", &null_handle);

	if (ACPI_FAILURE(status))
		return status;

	/* Fine, lets try to fuck it up, _GTF, _GTM, _STM and _SDD,
	 * we choose you! */
	if ((ACPI_SUCCESS(AcpiGetHandle(device->handle, "_GTF", &null_handle))) ||
		(ACPI_SUCCESS(AcpiGetHandle(device->handle, "_GTM", &null_handle))) ||
		(ACPI_SUCCESS(AcpiGetHandle(device->handle, "_STM", &null_handle))) ||
		(ACPI_SUCCESS(AcpiGetHandle(device->handle, "_SDD", &null_handle))))
		return AE_OK;

	/* Uh... ok... maybe we are sub-device of an ejectable parent */
	status = AcpiGetParent(device->handle, &parent_handle);

	if (ACPI_FAILURE(status))
		return status;

	/* Now, lets try to fuck up parent ! */
	if ((ACPI_SUCCESS(AcpiGetHandle(parent_handle, "_GTF", &null_handle))) ||
		(ACPI_SUCCESS(AcpiGetHandle(parent_handle, "_GTM", &null_handle))) ||
		(ACPI_SUCCESS(AcpiGetHandle(parent_handle, "_STM", &null_handle))) ||
		(ACPI_SUCCESS(AcpiGetHandle(parent_handle, "_SDD", &null_handle))))
		return AE_OK;

	return AE_NOT_FOUND;
}

/* Get Irq by Bus / Dev / Pin 
 * Returns -1 if no overrides exists */
int pci_device_get_irq(uint32_t bus, uint32_t device, uint32_t pin, 
	uint8_t *trigger_mode, uint8_t *polarity, uint8_t *shareable)
{
	/* Locate correct bus */
	int n = 0;
	pci_device_t *dev;
	
	while (1)
	{
		dev = (pci_device_t*)list_get_data_by_id(glb_pci_acpica, ACPI_BUS_ROOT_BRIDGE, n);

		if (dev == NULL)
			break;

		/* Todo, make sure we find the correct root-bridge */
		if (dev->routings != NULL)
		{
			/* Get offset */
			uint32_t toffset = (device * 4) + pin;

			/* Update IRQ Information */
			if (dev->routings->trigger[toffset] == ACPI_LEVEL_SENSITIVE)
				*trigger_mode = 1;
			else
				*trigger_mode = 0;

			if (dev->routings->polarity[toffset] == ACPI_ACTIVE_LOW)
				*polarity = 1;
			else
				*polarity = 0;

			*shareable = dev->routings->shareable[toffset];
			return dev->routings->interrupts[toffset];
		}
		
		/* Increase N */
		n++;
	}

	return -1;
}

/* Set Device Data Callback */
void pci_device_set_data_callback(ACPI_HANDLE handle, void *data)
{
	/* TODO */
}

/* Set Device Data */
ACPI_STATUS pci_device_set_data(pci_device_t *device, uint32_t type)
{
	/* Store, unless its power/sleep buttons */
	if ((type != ACPI_BUS_TYPE_POWER) &&
		(type != ACPI_BUS_TYPE_SLEEP))
	{
		return AcpiAttachData(device->handle, pci_device_set_data_callback, (void*)device);
	}

	return AE_OK;
}

/* Gets Device Status */
ACPI_STATUS pci_get_device_status(pci_device_t* device)
{
	ACPI_STATUS status = AE_OK;
	ACPI_BUFFER buffer;
	char lbuf[sizeof(ACPI_OBJECT)];

	/* Set up buffer */
	buffer.Length = sizeof(lbuf);
	buffer.Pointer = lbuf;

	/* Sanity */
	if (device->features & X86_ACPI_FEATURE_STA)
	{
		status = AcpiEvaluateObjectTyped(device->handle, "_STA", NULL, &buffer, ACPI_TYPE_INTEGER);

		/* Should not fail :( */
		if (ACPI_SUCCESS(status))
			device->status = (uint32_t)((ACPI_OBJECT *)buffer.Pointer)->Integer.Value;
		else
			device->status = 0;
	}
	else
	{
		/* The child in should not inherit the parents status if the parent is 
		 * functioning but not present (ie does not support dynamic status) */
		device->status = ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED |
							ACPI_STA_DEVICE_UI | ACPI_STA_DEVICE_FUNCTIONING;
	}

	return status;
}

/* Get Memory Configuration Range */
ACPI_STATUS pci_get_mem_config_range(pci_device_t *device)
{
	return (AE_OK);
}

/* Gets Device Bus Number */
ACPI_STATUS pci_get_device_bus_seg(pci_device_t* device)
{
	ACPI_STATUS status = AE_OK;
	ACPI_BUFFER buffer;
	char lbuf[sizeof(ACPI_OBJECT)];

	/* Set up buffer */
	buffer.Length = sizeof(lbuf);
	buffer.Pointer = lbuf;

	/* Sanity */
	if (device->features & X86_ACPI_FEATURE_BBN)
	{
		status = AcpiEvaluateObjectTyped(device->handle, "_BBN", NULL, &buffer, ACPI_TYPE_INTEGER);

		if (ACPI_SUCCESS(status))
			device->bus = (uint32_t)((ACPI_OBJECT *)buffer.Pointer)->Integer.Value;
		else
			device->bus = 0;
	}
	else
	{
		/* Bus number is 0-ish */
		device->bus = 0;
	}

	/* Sanity */
	if (device->features & X86_ACPI_FEATURE_SEG)
	{
		status = AcpiEvaluateObjectTyped(device->handle, "_SEG", NULL, &buffer, ACPI_TYPE_INTEGER);

		if (ACPI_SUCCESS(status))
			device->seg = (uint32_t)((ACPI_OBJECT *)buffer.Pointer)->Integer.Value;
		else
			device->seg = 0;
	}
	else
	{
		/* Bus number is 0-ish */
		device->seg = 0;
	}

	return status;
}

/* IRQ Routing Callback */
ACPI_STATUS pci_irq_routings_callback(ACPI_RESOURCE *res, void *context)
{
	pci_irq_resource_t *ires = (pci_irq_resource_t*)context;
	pci_device_t *device = (pci_device_t*)ires->device;
	ACPI_PCI_ROUTING_TABLE *tbl = (ACPI_PCI_ROUTING_TABLE*)ires->table;

	/* Normal IRQ Resource? */
	if (res->Type == ACPI_RESOURCE_TYPE_IRQ) 
	{
		ACPI_RESOURCE_IRQ *irq;
		UINT32 offset = (((tbl->Address >> 16) & 0xFF) * 4) + tbl->Pin;

		irq = &res->Data.Irq;
		device->routings->polarity[offset] = irq->Polarity;
		device->routings->trigger[offset] = irq->Triggering;
		device->routings->shareable[offset] = irq->Sharable;
		device->routings->interrupts[offset] = irq->Interrupts[tbl->SourceIndex];
	}
	else if (res->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ) 
	{
		ACPI_RESOURCE_EXTENDED_IRQ *irq;
		UINT32 offset = (((tbl->Address >> 16) & 0xFF) * 4) + tbl->Pin;

		irq = &res->Data.ExtendedIrq;
		device->routings->polarity[offset] = irq->Polarity;
		device->routings->trigger[offset] = irq->Triggering;
		device->routings->shareable[offset] = irq->Sharable;
		device->routings->interrupts[offset] = irq->Interrupts[tbl->SourceIndex];
	}

	return AE_OK;
}

/* Gets IRQ Routings */
ACPI_STATUS pci_get_device_irq_routings(pci_device_t *device)
{
	ACPI_STATUS status;
	ACPI_BUFFER abuff;
	ACPI_PCI_ROUTING_TABLE *tbl;
	int i;
	pci_routing_table_t *table;

	/* Setup Buffer */
	abuff.Length = 0x2000;
	abuff.Pointer = (char*)kmalloc(0x2000);

	/* Try to get routings */
	status = AcpiGetIrqRoutingTable(device->handle, &abuff);
	if (ACPI_FAILURE(status))
		goto done;

	/* Allocate Table */
	table = (pci_routing_table_t*)kmalloc(sizeof(pci_routing_table_t));
	
	/* Reset it */
	for (i = 0; i < 128; i++)
	{
		table->interrupts[i] = -1;
		table->polarity[i] = 0;
		table->shareable[i] = 0;
		table->trigger[i] = 0;
	}

	/* Link it */
	device->routings = table;

	/* Enumerate */
	for (tbl = (ACPI_PCI_ROUTING_TABLE *)abuff.Pointer; tbl->Length;
		tbl = (ACPI_PCI_ROUTING_TABLE *)
		((char *)tbl + tbl->Length)) 
	{
		ACPI_HANDLE src_handle;
		pci_irq_resource_t ires;

		/* Wub, we have a routing */
		if (*(char*)tbl->Source == '\0') 
		{
			/* Ok, eol */

			/* Set it */
			UINT32 offset = (((tbl->Address >> 16) & 0xFF) * 4) + tbl->Pin;
			table->interrupts[offset] = tbl->SourceIndex;
			continue;
		}

		/* Get handle of source */
		status = AcpiGetHandle(device->handle, tbl->Source, &src_handle);
		if (ACPI_FAILURE(status)) {
			printf("Failed AcpiGetHandle\n");
			continue;
		}

		/* Get all IRQ resources */
		ires.device = (void*)device;
		ires.table = (void*)tbl;
		
		status = AcpiWalkResources(src_handle, METHOD_NAME__CRS, pci_irq_routings_callback, &ires);
		
		if (ACPI_FAILURE(status)) {
			printf("Failed IRQ resource\n");
			continue;
		}
	}

done:
	kfree(abuff.Pointer);
	return AE_OK;
}

/* Gets Device Name */
ACPI_STATUS pci_get_bus_id(pci_device_t *device, uint32_t type)
{
	ACPI_STATUS status = AE_OK;
	ACPI_BUFFER buffer;
	char bus_id[8];

	/* Memset bus_id */
	memset(bus_id, 0, sizeof(bus_id));

	/* Setup Buffer */
	buffer.Pointer = bus_id;
	buffer.Length = sizeof(bus_id);

	/* Get Object Name based on type */
	switch (type)
	{
		case ACPI_BUS_SYSTEM:
			strcpy(device->bus_id, "ACPISB");
			break;
		case ACPI_BUS_TYPE_POWER:
			strcpy(device->bus_id, "POWERF");
			break;
		case ACPI_BUS_TYPE_SLEEP:
			strcpy(device->bus_id, "SLEEPF");
			break;
		default:
		{
			/* Get name */
			status = AcpiGetName(device->handle, ACPI_SINGLE_NAME, &buffer);

			/* Sanity */
			if (ACPI_SUCCESS(status))
				strcpy(device->bus_id, bus_id);
		} break;
	}

	return status;
}

/* Gets Device Features */
ACPI_STATUS pci_get_features(pci_device_t *device)
{
	ACPI_STATUS status;
	ACPI_HANDLE null_handle = NULL;

	/* Supports dynamic status? */
	status = AcpiGetHandle(device->handle, "_STA", &null_handle);
	
	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_STA;

	/* Is compatible ids present? */
	status = AcpiGetHandle(device->handle, "_CID", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_CID;

	/* Supports removable? */
	status = AcpiGetHandle(device->handle, "_RMV", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_RMV;

	/* Supports ejecting? */
	status = AcpiGetHandle(device->handle, "_EJD", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_EJD;
	else
	{
		status = AcpiGetHandle(device->handle, "_EJ0", &null_handle);

		if (ACPI_SUCCESS(status))
			device->features |= X86_ACPI_FEATURE_EJD;
	}

	/* Supports device locking? */
	status = AcpiGetHandle(device->handle, "_LCK", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_LCK;

	/* Supports power management? */
	status = AcpiGetHandle(device->handle, "_PS0", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_PS0;
	else
	{
		status = AcpiGetHandle(device->handle, "_PR0", &null_handle);

		if (ACPI_SUCCESS(status))
			device->features |= X86_ACPI_FEATURE_PS0;
	}

	/* Supports wake? */
	status = AcpiGetHandle(device->handle, "_PRW", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_PRW;
	
	/* Has IRQ Routing Table Present ?  */
	status = AcpiGetHandle(device->handle, "_PRT", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_PRT; 

	/* Supports Bus Numbering ?  */
	status = AcpiGetHandle(device->handle, "_BBN", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_BBN;

	/* Supports Bus Segment ?  */
	status = AcpiGetHandle(device->handle, "_SEG", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_SEG;

	/* Supports PCI Config Space ?  */
	status = AcpiGetHandle(device->handle, "_REG", &null_handle);

	if (ACPI_SUCCESS(status))
		device->features |= X86_ACPI_FEATURE_REG;

	return AE_OK;
}

/* Gets Device Information */
ACPI_STATUS pci_get_device_hw_info(pci_device_t *device, ACPI_HANDLE dev_parent, uint32_t type)
{
	ACPI_STATUS status;
	ACPI_DEVICE_INFO *dev_info;
	char lbuf[2048];
	ACPI_BUFFER buffer;
	ACPI_PNP_DEVICE_ID_LIST *cid = NULL;
	char *hid = NULL;
	char *uid = NULL;
	const char *cid_add = NULL;

	/* Memset buffer */
	memset(lbuf, 0, sizeof(lbuf));

	/* Set up initial variables */
	buffer.Length = sizeof(lbuf);
	buffer.Pointer = lbuf;
	dev_info = buffer.Pointer;

	/* What are we dealing with? */
	switch (type)
	{
		/* Normal Device */
		case ACPI_BUS_TYPE_DEVICE:
		{
			/* Get Object Info */
			status = AcpiGetObjectInfo(device->handle, &dev_info);

			/* Sanity */
			if (ACPI_FAILURE(status))
				return status;

			/* Get only valid fields */
			if (dev_info->Valid & ACPI_VALID_HID)
				hid = dev_info->HardwareId.String;
			if (dev_info->Valid & ACPI_VALID_UID)
				uid = dev_info->UniqueId.String;
			if (dev_info->Valid & ACPI_VALID_CID)
				cid = &dev_info->CompatibleIdList;
			if (dev_info->Valid & ACPI_VALID_ADR)
			{
				device->address = dev_info->Address;
				device->features |= X86_ACPI_FEATURE_ADR;
			}

			/* Check for special device, i.e Video / Bay / Dock */
			if (pci_device_is_video(device) == AE_OK)
				cid_add = "VIDEO";
			else if (pci_device_is_dock(device) == AE_OK)
				cid_add = "DOCK";
			else if (pci_device_is_bay(device) == AE_OK)
				cid_add = "BAY";
			
		} break;

		case ACPI_BUS_SYSTEM:
			hid = "LNXSYBUS";
			break;
		case ACPI_BUS_TYPE_POWER:
			hid = "LNXPWRBN";
			break;
		case ACPI_BUS_TYPE_PROCESSOR:
			hid = "LNXCPU";
			break;
		case ACPI_BUS_TYPE_SLEEP:
			hid = "LNXSLPBN";
			break;
		case ACPI_BUS_TYPE_THERMAL:
			hid = "LNXTHERM";
			break;
		case ACPI_BUS_TYPE_PWM:
			hid = "LNXPOWER";
			break;
	}

	/* Fix for Root System Bus (\_SB) */
	if (((ACPI_HANDLE)dev_parent == ACPI_ROOT_OBJECT) && (type == ACPI_BUS_TYPE_DEVICE)) 
		hid = "LNXSYSTM";

	/* Store HID and UID */
	if (hid) 
	{
		strcpy(device->hid, hid);
		device->features |= X86_ACPI_FEATURE_HID;
	}
	
	if (uid) 
	{
		strcpy(device->uid, uid);
		device->features |= X86_ACPI_FEATURE_UID;
	}
	
	/* Now store CID */
	if (cid != NULL || cid_add != NULL)
	{
		ACPI_PNP_DEVICE_ID_LIST *list;
		ACPI_SIZE size = 0;
		UINT32 count = 0;

		/* Get size if list exists */
		if (cid)
			size = cid->ListSize;
		else if (cid_add)
		{
			/* Allocate a bare structure */
			size = sizeof(ACPI_PNP_DEVICE_ID_LIST);
			cid = ACPI_ALLOCATE_ZEROED(size);

			/* Set */
			cid->ListSize = size;
			cid->Count = 0;
		}

		/* Do we need to manually add extra entry ? */
		if (cid_add)
			size += sizeof(ACPI_PNP_DEVICE_ID_LIST);

		/* Allocate new list */
		list = (ACPI_PNP_DEVICE_ID_LIST*)kmalloc((size_t)size);

		/* Copy list */
		if (cid)
		{
			memcpy(list, cid, cid->ListSize);
			count = cid->Count;
		}
		
		if (cid_add)
		{
			list->Ids[count].Length = sizeof(cid_add) + 1;
			list->Ids[count].String = (char*)kmalloc(sizeof(cid_add) + 1);
			strncpy(list->Ids[count].String, cid_add, sizeof(cid_add));
			count++;
		}

		/* Set information */
		list->Count = count;
		list->ListSize = size;
		device->cid = list;
		device->features |= X86_ACPI_FEATURE_CID;
	}

	return AE_OK;
}

/* Adds an object to the Acpi List */
pci_device_t *pci_add_object(ACPI_HANDLE handle, ACPI_HANDLE parent, uint32_t type)
{
	ACPI_STATUS status;
	pci_device_t *device;

	/* Allocate Resources */
	device = (pci_device_t*)kmalloc(sizeof(pci_device_t));

	/* Memset */
	memset(device, 0, sizeof(pci_device_t));

	/* Set handle */
	device->handle = handle;

	/* Get Bus Identifier */
	status = pci_get_bus_id(device, type);

	/* Which namespace functions is supported? */
	status = pci_get_features(device);

	/* Get Bus and Seg Number */
	status = pci_get_device_bus_seg(device);

	/* Check device status */
	switch (type)
	{
		/* Same handling for these */
		case ACPI_BUS_TYPE_DEVICE:
		case ACPI_BUS_TYPE_PROCESSOR:
		{
			/* Get Status */
			status = pci_get_device_status(device);

			if (ACPI_FAILURE(status))
			{
				printf("ACPI: Device %s failed its dynamic status check\n", device->bus_id);
				kfree(device);
				return NULL;
			}

			/* Is it present and functioning? */
			if (!(device->status & ACPI_STA_DEVICE_PRESENT) &&
				!(device->status & ACPI_STA_DEVICE_FUNCTIONING))
			{
				printf("ACPI: Device %s is not present or functioning\n", device->bus_id);
				kfree(device);
				return NULL;
			}
		}

		default:
			device->status = ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED |
								ACPI_STA_DEVICE_UI | ACPI_STA_DEVICE_FUNCTIONING;
	}

	/* Now, get HID, ADDR and UUID */
	status = pci_get_device_hw_info(device, parent, type);
	
	/* Make sure this call worked */
	if (ACPI_FAILURE(status))
	{
		printf("ACPI: Failed to retrieve object information about device %s\n", device->bus_id);
		kfree(device);
		return NULL;
	}

	/* Store the device structure with the object itself */
	status = pci_device_set_data(device, type);
	
	/* Convert ADR to device / function */
	if (device->features & X86_ACPI_FEATURE_ADR)
	{
		device->dev = ACPI_HIWORD(ACPI_LODWORD(device->address));
		device->func = ACPI_LOWORD(ACPI_LODWORD(device->address));

		/* Sanity Values */
		if (device->dev > 31)
			device->dev = 0;
		if (device->func > 8)
			device->func = 0;
	}
	else
	{
		device->dev = 0;
		device->func = 0;
	}

	/* Here we would handle all kinds of shizzle */
	/* printf("[%u:%u:%u]: %s (Name %s, Flags 0x%x)\n", device->bus,
		device->dev, device->func, device->hid, device->bus_id, device->features); */

	/* Does it contain routings */
	if (device->features & X86_ACPI_FEATURE_PRT)
	{
		status = pci_get_device_irq_routings(device);

		if (ACPI_FAILURE(status))
			printf("ACPI: Failed to retrieve pci irq routings from device %s\n", device->bus_id);
	}

	/* Is this root bus? */
	if (strncmp(device->hid, "PNP0A03", 7) == 0 ||
		strncmp(device->hid, "PNP0A08", 7) == 0)	/* PCI or PCI-express */
	{
		/* First, we have to negiotiate OS Control */
		//pci_negiotiate_os_control(device);

		/* OK so actually we can get the bus number from this, and then SIMPLY
		 * just use standard enumeration, wtf i did obviously fail at logic */
		pci_check_bus(glb_pci_devices, (uint8_t)device->bus);

		/* Save it root bridge list */
		device->type = ACPI_BUS_ROOT_BRIDGE;
		device->bus = glb_bus_counter;
		glb_bus_counter++;

		/* Perform PCI Config Space Initialization */
		AcpiInstallAddressSpaceHandler(device->handle, ACPI_ADR_SPACE_PCI_CONFIG, ACPI_DEFAULT_HANDLER, NULL, NULL);
	}
	else
		device->type = type;

	/* Add to list and return */
	list_append(glb_pci_acpica, list_create_node(device->type, device));

	return device;
}

/* Scan Callback */
ACPI_STATUS pci_scan_callback(ACPI_HANDLE handle, UINT32 level, void *context, void **return_value)
{
	pci_device_t *device = NULL;
	ACPI_STATUS status = AE_OK;
	ACPI_OBJECT_TYPE type = 0;
	ACPI_HANDLE parent = (ACPI_HANDLE)context;
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
	status = AcpiGetType(handle, &type);

	if (ACPI_FAILURE(status))
		return AE_OK;

	/* We dont want ALL types obviously */
	switch (type)
	{
		case ACPI_TYPE_DEVICE:
			type = ACPI_BUS_TYPE_DEVICE;
			break;
		case ACPI_TYPE_PROCESSOR:
			type = ACPI_BUS_TYPE_PROCESSOR;
			break;
		case ACPI_TYPE_THERMAL:
			type = ACPI_BUS_TYPE_THERMAL;
			break;
		case ACPI_TYPE_POWER:
			type = ACPI_BUS_TYPE_PWM;
		default:
			return AE_OK;
	}

	/* Get Parent */
	status = AcpiGetParent(handle, &parent);

	/* Add object to list */
	device = pci_add_object(handle, parent, type);
	
	/* Sanity */
	if (!device)
	{
		//acpi_scan_init_hotplug(device);
	}

	return AE_OK;
}

/* Scans a bus from a given start object */
void pci_scan_device_bus(ACPI_HANDLE handle)
{
	ACPI_STATUS status = AE_OK;

	/* Walk */
	//status = AcpiWalkNamespace(ACPI_TYPE_ANY, handle, ACPI_UINT32_MAX, pci_scan_callback, NULL, handle, NULL);
	status = AcpiGetDevices(NULL, pci_scan_callback, NULL, NULL);
}

void drivers_enumerate_acpica(void)
{
	/* Step 1. Enumerate Fixed Objects */
	if (AcpiGbl_FADT.Flags & ACPI_FADT_POWER_BUTTON)
		pci_add_object(NULL, ACPI_ROOT_OBJECT, ACPI_BUS_TYPE_POWER);

	if (AcpiGbl_FADT.Flags & ACPI_FADT_SLEEP_BUTTON)
		pci_add_object(NULL, ACPI_ROOT_OBJECT, ACPI_BUS_TYPE_SLEEP);

	/* Step 2. Enumerate bus */
	pci_scan_device_bus(ACPI_ROOT_OBJECT);
}

/* This enumerates EHCI controllers and makes sure all routing goes to
 * their companion controllers */
void drivers_disable_ehci(void *data, int n)
{
	pci_driver_t *driver = (pci_driver_t*)data;
	list_t *sub_bus;
	n = n;

	switch (driver->type)
	{
	case X86_PCI_TYPE_BRIDGE:
	{
		/* Get bus list */
		sub_bus = (list_t*)driver->children;

		/* Install drivers on that bus */
		list_execute_all(sub_bus, drivers_disable_ehci);

	} break;

	case X86_PCI_TYPE_DEVICE:
	{
		/* Get driver */

		/* Serial Bus Comms */
		if (driver->header->class_code == 0x0C)
		{
			/* Usb? */
			if (driver->header->subclass == 0x03)
			{
				/* Controller Type? */

				/* UHCI -> 0. OHCI -> 0x10. EHCI -> 0x20. xHCI -> 0x30 */
				if (driver->header->ProgIF == 0x20)
				{
					/* Initialise Controller */
					ehci_init(driver);
				}
			}
		}

	} break;

	default:
		break;
	}
}

/* This installs a driver for each device present (if we have a driver!) */
void drivers_setup_device(void *data, int n)
{
	pci_driver_t *driver = (pci_driver_t*)data;
	list_t *sub_bus; 

	/* We dont really use 'n' */
	_CRT_UNUSED(n);

	switch (driver->type)
	{
		case X86_PCI_TYPE_BRIDGE:
		{
			/* Get bus list */
			sub_bus = (list_t*)driver->children;

			/* Sanity */
			if (sub_bus == NULL || sub_bus->length == 0)
			{
				/* Something is up */
				break;
			}

			/* Install drivers on that bus */
			list_execute_all(sub_bus, drivers_setup_device);

		} break;

		case X86_PCI_TYPE_DEVICE:
		{
			/* Serial Bus Comms */
			if (driver->header->class_code == 0x0C)
			{
				/* Usb? */
				if (driver->header->subclass == 0x03)
				{
					/* Controller Type? */

					/* UHCI -> 0. OHCI -> 0x10. EHCI -> 0x20. xHCI -> 0x30 */

					if (driver->header->ProgIF == 0x0)
					{
						/* Initialise Controller */
						uhci_init(driver);
					}
					else if (driver->header->ProgIF == 0x10)
					{
						/* Initialise Controller */
						ohci_init(driver);
					}
				}
			}

		} break;

		default:
			break;
	}
}

/* Initialises all available drivers in system */
void drivers_init(void *args)
{
	/* Init list, this is "bus 0" */
	glb_pci_devices = list_create(LIST_SAFE);
	glb_pci_acpica = list_create(LIST_SAFE);

	/* Unused */
	_CRT_UNUSED(args);

	/* Start out by enumerating devices */
	printf("    * Enumerating PCI Space\n");
	drivers_enumerate_acpica();

	/* Special Step for EHCI Controllers 
	 * This is untill I know OHCI and UHCI works perfectly! */
	list_execute_all(glb_pci_devices, drivers_disable_ehci);

	/* Now, for each driver we have available install it */
	list_execute_all(glb_pci_devices, drivers_setup_device);

	/* Debug */
	printf("    * Device Enumeration Done!\n");
}