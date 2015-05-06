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
#include <Arch.h>

#include <assert.h>
#include <acpi.h>
#include <Pci.h>
#include <List.h>
#include <Heap.h>
#include <stddef.h>
#include <stdio.h>
#include <limits.h>

/* Drivers */
//#include <drivers\usb\uhci\uhci.h>
#include <Drivers\Usb\Ohci\Ohci.h>
#include <Drivers\Usb\Ehci\Ehci.h>

/* Prototypes */
void PciCheckFunction(list_t *Bridge, uint8_t Bus, uint8_t Device, uint8_t Function);
void PciCheckDevice(list_t *Bridge, uint8_t Bus, uint8_t Device);
void PciCheckBus(list_t *Bridge, uint8_t Bus);

PciAcpiDevice_t *PciAddObject(ACPI_HANDLE Handle, ACPI_HANDLE Parent, uint32_t Type);

/* Globals */
list_t *GlbPciDevices = NULL;
list_t *GlbPciAcpiDevices = NULL;
volatile uint32_t GlbBusCounter = 0;

/* Extern */
extern void ThreadingDebugPrint(void);

/* Check a function */
/* For each function we create a 
 * pci_device and add it to the list */
void PciCheckFunction(list_t *Bridge, uint8_t Bus, uint8_t Device, uint8_t Function)
{
	uint8_t sec_bus;
	PciNativeHeader_t *Pcs;
	PciDevice_t *PciDevice;

	/* Allocate */
	Pcs = (PciNativeHeader_t*)kmalloc(sizeof(PciNativeHeader_t));
	PciDevice = (PciDevice_t*)kmalloc(sizeof(PciDevice_t));

	/* Get full information */
	PciReadFunction(Pcs, Bus, Device, Function);

	/* Set information */
	PciDevice->Header = Pcs;
	PciDevice->Bus = Bus;
	PciDevice->Device = Device;
	PciDevice->Function = Function;
	PciDevice->Children = NULL;

	/* Info 
	 * Ignore the spam of device_id 0x7a0 in VMWare*/
	if (Pcs->DeviceId != 0x7a0)
	{
		printf("    * [%d:%d:%d][%d:%d:%d] Vendor 0x%x, Device 0x%x : %s\n",
			Pcs->Class, Pcs->Subclass, Pcs->Interface,
			Bus, Device, Function,
			Pcs->VendorId, Pcs->DeviceId,
			PciToString(Pcs->Class, Pcs->Subclass, Pcs->Interface));
	}

	/* Do some disabling */
	if ((Pcs->Class != 0x06) && (Pcs->Class != 0x03))
	{
		/* Disable Device untill further notice */
		PciWriteWord((const uint16_t)Bus, (const uint16_t)Device, (const uint16_t)Function, 0x04, 0x0400);
	}
	
	/* Add to list */
	if (Pcs->Class == 0x06 && Pcs->Subclass == 0x04)
	{
		PciDevice->Type = X86_PCI_TYPE_BRIDGE;
		list_append(Bridge, list_create_node(X86_PCI_TYPE_BRIDGE, PciDevice));
	}
	else
	{
		PciDevice->Type = X86_PCI_TYPE_DEVICE;
		list_append(Bridge, list_create_node(X86_PCI_TYPE_DEVICE, PciDevice));
	}

	/* Is this a secondary (PCI) bus */
	if ((Pcs->Class == 0x06) && (Pcs->Subclass == 0x04))
	{
		/* Uh oh, this dude has children */
		PciDevice->Children = list_create(LIST_SAFE);

		sec_bus = PciReadSecondaryBusNumber(Bus, Device, Function);
		PciCheckBus(PciDevice->Children, sec_bus);
	}
}

/* Check a device */
void PciCheckDevice(list_t *Bridge, uint8_t Bus, uint8_t Device)
{
	uint8_t Function = 0;
	uint16_t VendorId = 0;
	uint8_t HeaderType = 0;

	/* Get vendor id */
	VendorId = PciReadVendorId(Bus, Device, Function);

	/* Sanity */
	if (VendorId == 0xFFFF)
		return;

	/* Check function 0 */
	PciCheckFunction(Bridge, Bus, Device, Function);
	HeaderType = PciReadHeaderType(Bus, Device, Function);

	/* Multi-function or single? */
	if (HeaderType & 0x80)
	{
		/* It is a multi-function device, so check remaining functions */
		for (Function = 1; Function < 8; Function++)
		{
			/* Only check if valid vendor */
			if (PciReadVendorId(Bus, Device, Function) != 0xFFFF)
				PciCheckFunction(Bridge, Bus, Device, Function);
		}
	}
}

/* Check a bus */
void PciCheckBus(list_t *Bridge, uint8_t Bus)
{
	uint8_t Device;

	for (Device = 0; Device < 32; Device++)
		PciCheckDevice(Bridge, Bus, Device);
}

/* First of all, devices exists on TWO different
 * busses. PCI and PCI Express. */
void PciEnumerate(void)
{
	uint8_t Function;
	uint8_t Bus;
	uint8_t HeaderType;

	HeaderType = PciReadHeaderType(0, 0, 0);
	
	if ((HeaderType & 0x80) == 0)
	{
		/* Single PCI host controller */
		printf("    * Single Bus Present\n");
		PciCheckBus(GlbPciDevices, 0);
	}
	else 
	{
		/* Multiple PCI host controllers */
		printf("    * Multi Bus Present\n");
		for (Function = 0; Function < 8; Function++)
		{
			if (PciReadVendorId(0, 0, Function) != 0xFFFF)
				break;

			/* Check bus */
			Bus = Function;
			PciCheckBus(GlbPciDevices, Bus);
		}
	}
}

/* Video Backlight Capability Callback */
ACPI_STATUS PciVideoBacklightCapCallback(ACPI_HANDLE Handle, UINT32 Level, void *Context, void **ReturnValue)
{
	ACPI_HANDLE NullHandle = NULL;
	uint64_t *video_features = (uint64_t*)Context;

	if (ACPI_SUCCESS(AcpiGetHandle(Handle, "_BCM", &NullHandle)) &&
		ACPI_SUCCESS(AcpiGetHandle(Handle, "_BCL", &NullHandle)))
	{
		/* Add */
		*video_features |= ACPI_VIDEO_BACKLIGHT;
		
		/* Check for Brightness */
		if (ACPI_SUCCESS(AcpiGetHandle(Handle, "_BQC", &NullHandle)))
			*video_features |= ACPI_VIDEO_BRIGHTNESS;
		
		/* We have backlight support, no need to scan further */
		return AE_CTRL_TERMINATE;
	}

	return AE_OK;
}

/* Is this a video device? 
 * If it is, we also retrieve capabilities */
ACPI_STATUS PciDeviceIsVideo(PciAcpiDevice_t *Device)
{
	ACPI_HANDLE NullHandle = NULL;
	uint64_t video_features = 0;

	/* Sanity */
	if (Device == NULL)
		return AE_ABORT_METHOD;

	/* Does device support Video Switching */
	if (ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_DOD", &NullHandle)) &&
		ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_DOS", &NullHandle)))
		video_features |= ACPI_VIDEO_SWITCHING;

	/* Does device support Video Rom? */
	if (ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_ROM", &NullHandle)))
		video_features |= ACPI_VIDEO_ROM;

	/* Does device support configurable video head? */
	if (ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_VPO", &NullHandle)) &&
		ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_GPD", &NullHandle)) &&
		ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_SPD", &NullHandle)))
		video_features |= ACPI_VIDEO_POSTING;

	/* Only call this if it is a video device */
	if (video_features != 0)
	{
		AcpiWalkNamespace(ACPI_TYPE_DEVICE, Device->Handle, ACPI_UINT32_MAX,
			PciVideoBacklightCapCallback, NULL, &video_features, NULL);

		/* Update ONLY if video device */
		Device->xFeatures = video_features;

		return AE_OK;
	}
	else 
		return AE_NOT_FOUND;
}

/* Is this a docking device? 
 * If it has a _DCK method, yes */
ACPI_STATUS PciDeviceIsDock(PciAcpiDevice_t *Device)
{
	ACPI_HANDLE NullHandle = NULL;

	/* Sanity */
	if (Device == NULL)
		return AE_ABORT_METHOD;
	else
		return AcpiGetHandle(Device->Handle, "_DCK", &NullHandle);
}

/* Is this a BAY (i.e cd-rom drive with a ejectable bay) 
 * We check several ACPI methods here */
ACPI_STATUS PciDeviceIsBay(PciAcpiDevice_t *Device)
{
	ACPI_STATUS status;
	ACPI_HANDLE ParentHandle = NULL;
	ACPI_HANDLE NullHandle = NULL;

	/* Sanity, make sure it is even ejectable */
	status = AcpiGetHandle(Device->Handle, "_EJ0", &NullHandle);

	if (ACPI_FAILURE(status))
		return status;

	/* Fine, lets try to fuck it up, _GTF, _GTM, _STM and _SDD,
	 * we choose you! */
	if ((ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_GTF", &NullHandle))) ||
		(ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_GTM", &NullHandle))) ||
		(ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_STM", &NullHandle))) ||
		(ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_SDD", &NullHandle))))
		return AE_OK;

	/* Uh... ok... maybe we are sub-device of an ejectable parent */
	status = AcpiGetParent(Device->Handle, &ParentHandle);

	if (ACPI_FAILURE(status))
		return status;

	/* Now, lets try to fuck up parent ! */
	if ((ACPI_SUCCESS(AcpiGetHandle(ParentHandle, "_GTF", &NullHandle))) ||
		(ACPI_SUCCESS(AcpiGetHandle(ParentHandle, "_GTM", &NullHandle))) ||
		(ACPI_SUCCESS(AcpiGetHandle(ParentHandle, "_STM", &NullHandle))) ||
		(ACPI_SUCCESS(AcpiGetHandle(ParentHandle, "_SDD", &NullHandle))))
		return AE_OK;

	return AE_NOT_FOUND;
}

/* Get Irq by Bus / Dev / Pin 
 * Returns -1 if no overrides exists */
int PciDeviceGetIrq(uint32_t Bus, uint32_t Device, uint32_t Pin, 
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

/* Set Device Data Callback */
void PciDeviceSetDataCallback(ACPI_HANDLE Handle, void *Data)
{
	/* TODO */
	_CRT_UNUSED(Handle);
	_CRT_UNUSED(Data);
}

/* Set Device Data */
ACPI_STATUS PciDeviceAttachData(PciAcpiDevice_t *Device, uint32_t Type)
{
	/* Store, unless its power/sleep buttons */
	if ((Type != ACPI_BUS_TYPE_POWER) &&
		(Type != ACPI_BUS_TYPE_SLEEP))
	{
		return AcpiAttachData(Device->Handle, PciDeviceSetDataCallback, (void*)Device);
	}

	return AE_OK;
}

/* Gets Device Status */
ACPI_STATUS PciDeviceGetStatus(PciAcpiDevice_t* Device)
{
	ACPI_STATUS Status = AE_OK;
	ACPI_BUFFER Buffer;
	char lbuf[sizeof(ACPI_OBJECT)];

	/* Set up buffer */
	Buffer.Length = sizeof(lbuf);
	Buffer.Pointer = lbuf;

	/* Sanity */
	if (Device->Features & X86_ACPI_FEATURE_STA)
	{
		Status = AcpiEvaluateObjectTyped(Device->Handle, "_STA", NULL, &Buffer, ACPI_TYPE_INTEGER);

		/* Should not fail :( */
		if (ACPI_SUCCESS(Status))
			Device->Status = (uint32_t)((ACPI_OBJECT *)Buffer.Pointer)->Integer.Value;
		else
			Device->Status = 0;
	}
	else
	{
		/* The child in should not inherit the parents status if the parent is 
		 * functioning but not present (ie does not support dynamic status) */
		Device->Status = ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED |
							ACPI_STA_DEVICE_UI | ACPI_STA_DEVICE_FUNCTIONING;
	}

	return Status;
}

/* Get Memory Configuration Range */
ACPI_STATUS PciDeviceGetMemConfigRange(PciAcpiDevice_t *Device)
{
	/* Unused */
	_CRT_UNUSED(Device);

	return (AE_OK);
}

/* Gets Device Bus Number */
ACPI_STATUS PciDeviceGetBusAndSegment(PciAcpiDevice_t* Device)
{
	ACPI_STATUS Status = AE_OK;
	ACPI_BUFFER Buffer;
	char lbuf[sizeof(ACPI_OBJECT)];

	/* Set up buffer */
	Buffer.Length = sizeof(lbuf);
	Buffer.Pointer = lbuf;

	/* Sanity */
	if (Device->Features & X86_ACPI_FEATURE_BBN)
	{
		Status = AcpiEvaluateObjectTyped(Device->Handle, "_BBN", NULL, &Buffer, ACPI_TYPE_INTEGER);

		if (ACPI_SUCCESS(Status))
			Device->Bus = (uint32_t)((ACPI_OBJECT *)Buffer.Pointer)->Integer.Value;
		else
			Device->Bus = 0;
	}
	else
	{
		/* Bus number is 0-ish */
		Device->Bus = 0;
	}

	/* Sanity */
	if (Device->Features & X86_ACPI_FEATURE_SEG)
	{
		Status = AcpiEvaluateObjectTyped(Device->Handle, "_SEG", NULL, &Buffer, ACPI_TYPE_INTEGER);

		if (ACPI_SUCCESS(Status))
			Device->Segment = (uint32_t)((ACPI_OBJECT *)Buffer.Pointer)->Integer.Value;
		else
			Device->Segment = 0;
	}
	else
	{
		/* Bus number is 0-ish */
		Device->Segment = 0;
	}

	return Status;
}

/* IRQ Routing Callback */
ACPI_STATUS PciDeviceIrqRoutingCallback(ACPI_RESOURCE *Resource, void *Context)
{
	PciIrqResource_t *IrqResource = (PciIrqResource_t*)Context;
	PciAcpiDevice_t *Device = (PciAcpiDevice_t*)IrqResource->Device;
	ACPI_PCI_ROUTING_TABLE *IrqTable = (ACPI_PCI_ROUTING_TABLE*)IrqResource->Table;

	/* Normal IRQ Resource? */
	if (Resource->Type == ACPI_RESOURCE_TYPE_IRQ)
	{
		ACPI_RESOURCE_IRQ *Irq;
		UINT32 offset = ((ACPI_HIWORD(ACPI_LODWORD(IrqTable->Address))) * 4) + IrqTable->Pin;

		Irq = &Resource->Data.Irq;
		Device->Routings->Polarity[offset] = Irq->Polarity;
		Device->Routings->Trigger[offset] = Irq->Triggering;
		Device->Routings->Shareable[offset] = Irq->Sharable;
		Device->Routings->Interrupts[offset] = Irq->Interrupts[IrqTable->SourceIndex];

		/* Debug */
		printf("(Device %u - Pin: %u, Irq: %u) ",
			(uint32_t)ACPI_HIWORD(ACPI_LODWORD(IrqTable->Address)),
			IrqTable->Pin, Irq->Interrupts[IrqTable->SourceIndex]);
	}
	else if (Resource->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ)
	{
		ACPI_RESOURCE_EXTENDED_IRQ *Irq;
		UINT32 offset = ((ACPI_HIWORD(ACPI_LODWORD(IrqTable->Address))) * 4) + IrqTable->Pin;

		Irq = &Resource->Data.ExtendedIrq;
		Device->Routings->Polarity[offset] = Irq->Polarity;
		Device->Routings->Trigger[offset] = Irq->Triggering;
		Device->Routings->Shareable[offset] = Irq->Sharable;
		Device->Routings->Interrupts[offset] = Irq->Interrupts[IrqTable->SourceIndex];

		/* Debug */
		printf("(Device %u - Pin: %u, Irq: %u) ",
			(uint32_t)ACPI_HIWORD(ACPI_LODWORD(IrqTable->Address)),
			IrqTable->Pin, Irq->Interrupts[IrqTable->SourceIndex]);
	}

	return AE_OK;
}

/* Gets IRQ Routings */
ACPI_STATUS PciDeviceGetIrqRoutings(PciAcpiDevice_t *Device)
{
	ACPI_STATUS Status;
	ACPI_BUFFER aBuff;
	ACPI_PCI_ROUTING_TABLE *PciTable;
	int i;
	PciRoutings_t *Table;

	/* Setup Buffer */
	aBuff.Length = 0x2000;
	aBuff.Pointer = (char*)kmalloc(0x2000);

	/* Try to get routings */
	Status = AcpiGetIrqRoutingTable(Device->Handle, &aBuff);
	if (ACPI_FAILURE(Status))
		goto done;

	/* Allocate Table */
	Table = (PciRoutings_t*)kmalloc(sizeof(PciRoutings_t));
	
	/* Reset it */
	for (i = 0; i < 128; i++)
	{
		Table->Interrupts[i] = -1;
		Table->Polarity[i] = 0;
		Table->Shareable[i] = 0;
		Table->Trigger[i] = 0;
		Table->Fixed[i] = 0;
	}

	/* Link it */
	Device->Routings = Table;

	/* Enumerate */
	for (PciTable = (ACPI_PCI_ROUTING_TABLE *)aBuff.Pointer; PciTable->Length;
		PciTable = (ACPI_PCI_ROUTING_TABLE *)
		((char *)PciTable + PciTable->Length))
	{
		ACPI_HANDLE SourceHandle;
		PciIrqResource_t IrqRes;

		/* Wub, we have a routing */
		if (*(char*)PciTable->Source == '\0')
		{
			/* Ok, eol */
			
			/* Set it */
			UINT32 offset = ((ACPI_HIWORD(ACPI_LODWORD(PciTable->Address))) * 4) + PciTable->Pin;

			/* Fixed GSI */
			Table->Interrupts[offset] = PciTable->SourceIndex;
			Table->Polarity[offset] = ACPI_ACTIVE_LOW;
			Table->Trigger[offset] = ACPI_LEVEL_SENSITIVE;
			Table->Fixed[offset] = 1;
			continue;
		}

		/* Get handle of source */
		Status = AcpiGetHandle(Device->Handle, PciTable->Source, &SourceHandle);
		if (ACPI_FAILURE(Status)) {
			printf("Failed AcpiGetHandle\n");
			continue;
		}

		/* Get all IRQ resources */
		IrqRes.Device = (void*)Device;
		IrqRes.Table = (void*)PciTable;
		
		Status = AcpiWalkResources(SourceHandle, METHOD_NAME__CRS, PciDeviceIrqRoutingCallback, &IrqRes);
		
		if (ACPI_FAILURE(Status)) {
			printf("Failed IRQ resource\n");
			continue;
		}
	}

done:
	kfree(aBuff.Pointer);
	return Status;
}

/* Gets Device Name */
ACPI_STATUS PciDeviceGetBusId(PciAcpiDevice_t *Device, uint32_t Type)
{
	ACPI_STATUS Status = AE_OK;
	ACPI_BUFFER Buffer;
	char BusId[8];

	/* Memset bus_id */
	memset(BusId, 0, sizeof(BusId));

	/* Setup Buffer */
	Buffer.Pointer = BusId;
	Buffer.Length = sizeof(BusId);

	/* Get Object Name based on type */
	switch (Type)
	{
		case ACPI_BUS_SYSTEM:
			strcpy(Device->BusId, "ACPISB");
			break;
		case ACPI_BUS_TYPE_POWER:
			strcpy(Device->BusId, "POWERF");
			break;
		case ACPI_BUS_TYPE_SLEEP:
			strcpy(Device->BusId, "SLEEPF");
			break;
		default:
		{
			/* Get name */
			Status = AcpiGetName(Device->Handle, ACPI_SINGLE_NAME, &Buffer);

			/* Sanity */
			if (ACPI_SUCCESS(Status))
				strcpy(Device->BusId, BusId);
		} break;
	}

	return Status;
}

/* Gets Device Features */
ACPI_STATUS PciDeviceGetFeatures(PciAcpiDevice_t *Device)
{
	ACPI_STATUS Status;
	ACPI_HANDLE NullHandle = NULL;

	/* Supports dynamic status? */
	Status = AcpiGetHandle(Device->Handle, "_STA", &NullHandle);
	
	if (ACPI_SUCCESS(Status))
		Device->Features |= X86_ACPI_FEATURE_STA;

	/* Is compatible ids present? */
	Status = AcpiGetHandle(Device->Handle, "_CID", &NullHandle);

	if (ACPI_SUCCESS(Status))
		Device->Features |= X86_ACPI_FEATURE_CID;

	/* Supports removable? */
	Status = AcpiGetHandle(Device->Handle, "_RMV", &NullHandle);

	if (ACPI_SUCCESS(Status))
		Device->Features |= X86_ACPI_FEATURE_RMV;

	/* Supports ejecting? */
	Status = AcpiGetHandle(Device->Handle, "_EJD", &NullHandle);

	if (ACPI_SUCCESS(Status))
		Device->Features |= X86_ACPI_FEATURE_EJD;
	else
	{
		Status = AcpiGetHandle(Device->Handle, "_EJ0", &NullHandle);

		if (ACPI_SUCCESS(Status))
			Device->Features |= X86_ACPI_FEATURE_EJD;
	}

	/* Supports device locking? */
	Status = AcpiGetHandle(Device->Handle, "_LCK", &NullHandle);

	if (ACPI_SUCCESS(Status))
		Device->Features |= X86_ACPI_FEATURE_LCK;

	/* Supports power management? */
	Status = AcpiGetHandle(Device->Handle, "_PS0", &NullHandle);

	if (ACPI_SUCCESS(Status))
		Device->Features |= X86_ACPI_FEATURE_PS0;
	else
	{
		Status = AcpiGetHandle(Device->Handle, "_PR0", &NullHandle);

		if (ACPI_SUCCESS(Status))
			Device->Features |= X86_ACPI_FEATURE_PS0;
	}

	/* Supports wake? */
	Status = AcpiGetHandle(Device->Handle, "_PRW", &NullHandle);

	if (ACPI_SUCCESS(Status))
		Device->Features |= X86_ACPI_FEATURE_PRW;
	
	/* Has IRQ Routing Table Present ?  */
	Status = AcpiGetHandle(Device->Handle, "_PRT", &NullHandle);

	if (ACPI_SUCCESS(Status))
		Device->Features |= X86_ACPI_FEATURE_PRT;

	/* Has Current Resources Set ?  */
	Status = AcpiGetHandle(Device->Handle, "_CRS", &NullHandle);

	if (ACPI_SUCCESS(Status))
		Device->Features |= X86_ACPI_FEATURE_CRS;

	/* Supports Bus Numbering ?  */
	Status = AcpiGetHandle(Device->Handle, "_BBN", &NullHandle);

	if (ACPI_SUCCESS(Status))
		Device->Features |= X86_ACPI_FEATURE_BBN;

	/* Supports Bus Segment ?  */
	Status = AcpiGetHandle(Device->Handle, "_SEG", &NullHandle);

	if (ACPI_SUCCESS(Status))
		Device->Features |= X86_ACPI_FEATURE_SEG;

	/* Supports PCI Config Space ?  */
	Status = AcpiGetHandle(Device->Handle, "_REG", &NullHandle);

	if (ACPI_SUCCESS(Status))
		Device->Features |= X86_ACPI_FEATURE_REG;

	return AE_OK;
}

/* Gets Device Information */
ACPI_STATUS PciDeviceGetHWInfo(PciAcpiDevice_t *Device, ACPI_HANDLE ParentHandle, uint32_t Type)
{
	ACPI_STATUS Status;
	ACPI_DEVICE_INFO *DeviceInfo;
	char lbuf[2048];
	ACPI_BUFFER Buffer;
	ACPI_PNP_DEVICE_ID_LIST *Cid = NULL;
	char *Hid = NULL;
	char *Uid = NULL;
	const char *CidAdd = NULL;

	/* Memset buffer */
	memset(lbuf, 0, sizeof(lbuf));

	/* Set up initial variables */
	Buffer.Length = sizeof(lbuf);
	Buffer.Pointer = lbuf;
	DeviceInfo = Buffer.Pointer;

	/* What are we dealing with? */
	switch (Type)
	{
		/* Normal Device */
		case ACPI_BUS_TYPE_DEVICE:
		{
			/* Get Object Info */
			Status = AcpiGetObjectInfo(Device->Handle, &DeviceInfo);

			/* Sanity */
			if (ACPI_FAILURE(Status))
				return Status;

			/* Get only valid fields */
			if (DeviceInfo->Valid & ACPI_VALID_HID)
				Hid = DeviceInfo->HardwareId.String;
			if (DeviceInfo->Valid & ACPI_VALID_UID)
				Uid = DeviceInfo->UniqueId.String;
			if (DeviceInfo->Valid & ACPI_VALID_CID)
				Cid = &DeviceInfo->CompatibleIdList;
			if (DeviceInfo->Valid & ACPI_VALID_ADR)
			{
				Device->Address = DeviceInfo->Address;
				Device->Features |= X86_ACPI_FEATURE_ADR;
			}

			/* Check for special device, i.e Video / Bay / Dock */
			if (PciDeviceIsVideo(Device) == AE_OK)
				CidAdd = "VIDEO";
			else if (PciDeviceIsDock(Device) == AE_OK)
				CidAdd = "DOCK";
			else if (PciDeviceIsBay(Device) == AE_OK)
				CidAdd = "BAY";
			
		} break;

		case ACPI_BUS_SYSTEM:
			Hid = "LNXSYBUS";
			break;
		case ACPI_BUS_TYPE_POWER:
			Hid = "LNXPWRBN";
			break;
		case ACPI_BUS_TYPE_PROCESSOR:
			Hid = "LNXCPU";
			break;
		case ACPI_BUS_TYPE_SLEEP:
			Hid = "LNXSLPBN";
			break;
		case ACPI_BUS_TYPE_THERMAL:
			Hid = "LNXTHERM";
			break;
		case ACPI_BUS_TYPE_PWM:
			Hid = "LNXPOWER";
			break;
	}

	/* Fix for Root System Bus (\_SB) */
	if (((ACPI_HANDLE)ParentHandle == ACPI_ROOT_OBJECT) && (Type == ACPI_BUS_TYPE_DEVICE))
		Hid = "LNXSYSTM";

	/* Store HID and UID */
	if (Hid)
	{
		strcpy(Device->HID, Hid);
		Device->Features |= X86_ACPI_FEATURE_HID;
	}
	
	if (Uid) 
	{
		strcpy(Device->UID, Uid);
		Device->Features |= X86_ACPI_FEATURE_UID;
	}
	
	/* Now store CID */
	if (Cid != NULL || CidAdd != NULL)
	{
		ACPI_PNP_DEVICE_ID_LIST *list;
		ACPI_SIZE size = 0;
		UINT32 count = 0;

		/* Get size if list exists */
		if (Cid)
			size = Cid->ListSize;
		else if (CidAdd)
		{
			/* Allocate a bare structure */
			size = sizeof(ACPI_PNP_DEVICE_ID_LIST);
			Cid = ACPI_ALLOCATE_ZEROED(size);

			/* Set */
			Cid->ListSize = size;
			Cid->Count = 0;
		}

		/* Do we need to manually add extra entry ? */
		if (CidAdd)
			size += sizeof(ACPI_PNP_DEVICE_ID_LIST);

		/* Allocate new list */
		list = (ACPI_PNP_DEVICE_ID_LIST*)kmalloc((size_t)size);

		/* Copy list */
		if (Cid)
		{
			memcpy(list, Cid, Cid->ListSize);
			count = Cid->Count;
		}
		
		if (CidAdd)
		{
			list->Ids[count].Length = sizeof(CidAdd) + 1;
			list->Ids[count].String = (char*)kmalloc(sizeof(CidAdd) + 1);
			strncpy(list->Ids[count].String, CidAdd, sizeof(CidAdd));
			count++;
		}

		/* Set information */
		list->Count = count;
		list->ListSize = size;
		Device->CID = list;
		Device->Features |= X86_ACPI_FEATURE_CID;
	}

	return AE_OK;
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
	Status = PciDeviceGetBusId(Device, Type);

	/* Which namespace functions is supported? */
	Status = PciDeviceGetFeatures(Device);

	/* Get Bus and Seg Number */
	Status = PciDeviceGetBusAndSegment(Device);

	/* Check device status */
	switch (Type)
	{
		/* Same handling for these */
		case ACPI_BUS_TYPE_DEVICE:
		case ACPI_BUS_TYPE_PROCESSOR:
		{
			/* Get Status */
			Status = PciDeviceGetStatus(Device);

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
	Status = PciDeviceGetHWInfo(Device, Parent, Type);
	
	/* Make sure this call worked */
	if (ACPI_FAILURE(Status))
	{
		printf("ACPI: Failed to retrieve object information about device %s\n", Device->BusId);
		kfree(Device);
		return NULL;
	}

	/* Store the device structure with the object itself */
	Status = PciDeviceAttachData(Device, Type);
	
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
		Status = PciDeviceGetIrqRoutings(Device);

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

/* Scans a bus from a given start object */
void PciScanBus(ACPI_HANDLE Handle)
{
	ACPI_STATUS Status = AE_OK;

	/* Walk */
	//status = AcpiWalkNamespace(ACPI_TYPE_ANY, handle, ACPI_UINT32_MAX, pci_scan_callback, NULL, handle, NULL);
	Status = AcpiGetDevices(NULL, PciScanCallback, NULL, NULL);
}

void PciAcpiEnumerate(void)
{
	/* Step 1. Enumerate Fixed Objects */
	if (AcpiGbl_FADT.Flags & ACPI_FADT_POWER_BUTTON)
		PciAddObject(NULL, ACPI_ROOT_OBJECT, ACPI_BUS_TYPE_POWER);

	if (AcpiGbl_FADT.Flags & ACPI_FADT_SLEEP_BUTTON)
		PciAddObject(NULL, ACPI_ROOT_OBJECT, ACPI_BUS_TYPE_SLEEP);

	/* Step 2. Enumerate bus */
	PciScanBus(ACPI_ROOT_OBJECT);
}

/* This enumerates EHCI controllers and makes sure all routing goes to
 * their companion controllers */
void DriverDisableEhci(void *Data, int n)
{
	PciDevice_t *driver = (PciDevice_t*)Data;
	list_t *sub_bus;
	n = n;

	switch (driver->Type)
	{
	case X86_PCI_TYPE_BRIDGE:
	{
		/* Get bus list */
		sub_bus = (list_t*)driver->Children;

		/* Install drivers on that bus */
		list_execute_all(sub_bus, DriverDisableEhci);

	} break;

	case X86_PCI_TYPE_DEVICE:
	{
		/* Get driver */

		/* Serial Bus Comms */
		if (driver->Header->Class == 0x0C)
		{
			/* Usb? */
			if (driver->Header->Subclass == 0x03)
			{
				/* Controller Type? */

				/* UHCI -> 0. OHCI -> 0x10. EHCI -> 0x20. xHCI -> 0x30 */
				if (driver->Header->Interface == 0x20)
				{
					/* Initialise Controller */
					EhciInit(driver);
				}
			}
		}

	} break;

	default:
		break;
	}
}

/* This installs a driver for each device present (if we have a driver!) */
void DriverSetupCallback(void *Data, int n)
{
	PciDevice_t *driver = (PciDevice_t*)Data;
	list_t *sub_bus; 

	/* We dont really use 'n' */
	_CRT_UNUSED(n);

	switch (driver->Type)
	{
		case X86_PCI_TYPE_BRIDGE:
		{
			/* Get bus list */
			sub_bus = (list_t*)driver->Children;

			/* Sanity */
			if (sub_bus == NULL || sub_bus->length == 0)
			{
				/* Something is up */
				break;
			}

			/* Install drivers on that bus */
			list_execute_all(sub_bus, DriverSetupCallback);

		} break;

		case X86_PCI_TYPE_DEVICE:
		{
			/* Serial Bus Comms */
			if (driver->Header->Class == 0x0C)
			{
				/* Usb? */
				if (driver->Header->Subclass == 0x03)
				{
					/* Controller Type? */

					/* UHCI -> 0. OHCI -> 0x10. EHCI -> 0x20. xHCI -> 0x30 */

					if (driver->Header->Interface == 0x0)
					{
						/* Initialise Controller */
						//uhci_init(driver);
					}
					else if (driver->Header->Interface == 0x10)
					{
						/* Initialise Controller */
						OhciInit(driver);
					}
				}
			}

		} break;

		default:
			break;
	}
}

/* Initialises all available drivers in system */
void DriverManagerInit(void *Args)
{
	/* Init list, this is "bus 0" */
	GlbPciDevices = list_create(LIST_SAFE);
	GlbPciAcpiDevices = list_create(LIST_SAFE);

	/* Unused */
	_CRT_UNUSED(Args);

	/* Start out by enumerating devices */
	printf("    * Enumerating PCI Space\n");
	PciAcpiEnumerate();

	/* Debug */
	printf("    * Device Enumeration Done!\n");

	/* Special Step for EHCI Controllers
	* This is untill I know OHCI and UHCI works perfectly! */
	list_execute_all(GlbPciDevices, DriverDisableEhci);

	printf("Installing Drivers\n");

	/* Now, for each driver we have available install it */
	list_execute_all(GlbPciDevices, DriverSetupCallback);
}