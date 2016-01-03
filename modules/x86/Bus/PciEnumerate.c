/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS X86 Pci Driver
*/

/* Arch */
#include <x86\AcpiSys.h>
#include <x86\Memory.h>

/* Includes */
#include <DeviceManager.h>
#include <Module.h>
#include "Pci.h"
#include <List.h>
#include <Heap.h>

/* The MCFG Entry */
#pragma pack(push, 1)
typedef struct _McfgEntry
{
	/* Base Address */
	uint64_t BaseAddress;

	/* Pci Segment Group */
	uint16_t SegmentGroup;

	/* Bus Range */
	uint8_t StartBus;
	uint8_t EndBus;

	/* Unused */
	uint32_t Reserved;

} McfgEntry_t; 
#pragma pack(pop)

/* Globals */
list_t *GlbPciDevices = NULL;

/* Prototypes */
void PciCheckBus(list_t *Bridge, PciBus_t *Bus, uint8_t BusNo);

/* Check a function */
/* For each function we create a 
 * pci_device and add it to the list */
void PciCheckFunction(list_t *Bridge, PciBus_t *BusIo, uint8_t Bus, uint8_t Device, uint8_t Function)
{
	/* Vars */
	MCoreDevice_t *mDevice;
	uint8_t SecondBus;
	PciNativeHeader_t *Pcs;
	PciDevice_t *PciDevice;

	/* Allocate */
	Pcs = (PciNativeHeader_t*)kmalloc(sizeof(PciNativeHeader_t));
	PciDevice = (PciDevice_t*)kmalloc(sizeof(PciDevice_t));

	/* Get full information */
	PciReadFunction(Pcs, BusIo, Bus, Device, Function);

	/* Set information */
	PciDevice->PciBus = BusIo;
	PciDevice->Header = Pcs;
	PciDevice->Bus = Bus;
	PciDevice->Device = Device;
	PciDevice->Function = Function;
	PciDevice->Children = NULL;

	/* Info 
	 * Ignore the spam of device_id 0x7a0 in VMWare*/
	if (Pcs->DeviceId != 0x7a0)
	{
#ifdef X86_PCI_DIAGNOSE
		printf("    * [%d:%d:%d][%d:%d:%d] Vendor 0x%x, Device 0x%x : %s\n",
			Pcs->Class, Pcs->Subclass, Pcs->Interface,
			Bus, Device, Function,
			Pcs->VendorId, Pcs->DeviceId,
			PciToString(Pcs->Class, Pcs->Subclass, Pcs->Interface));
#endif
	}

	/* Do some disabling, but NOT on the video or bridge */
	if ((Pcs->Class != PCI_DEVICE_CLASS_BRIDGE) && (Pcs->Class != PCI_DEVICE_CLASS_VIDEO))
	{
		/* Disable Device untill further notice */
		uint16_t PciSettings = PciRead16(BusIo, Bus, Device, Function, 0x04);
		PciWrite16(BusIo, Bus, Device, Function, 0x04, PciSettings | X86_PCI_COMMAND_INTDISABLE);
	}
	
	/* Add to list */
	if (Pcs->Class == PCI_DEVICE_CLASS_BRIDGE && Pcs->Subclass == PCI_DEVICE_SUBCLASS_PCI)
	{
		PciDevice->Type = X86_PCI_TYPE_BRIDGE;
		list_append(Bridge, list_create_node(X86_PCI_TYPE_BRIDGE, PciDevice));
	}
	else
	{
		/* This is an device */
		PciDevice->Type = X86_PCI_TYPE_DEVICE;
		list_append(Bridge, list_create_node(X86_PCI_TYPE_DEVICE, PciDevice));

		/* Register device with the OS */
		mDevice = (MCoreDevice_t*)kmalloc(sizeof(MCoreDevice_t));
		
		/* Setup information */
		mDevice->VendorId = Pcs->VendorId;
		mDevice->DeviceId = Pcs->DeviceId;
		mDevice->Class = Pcs->Class;
		mDevice->Subclass = Pcs->Subclass;
		mDevice->Interface = Pcs->Interface;

		/* Type */
		mDevice->Type = DeviceUnknown;
		mDevice->Data = NULL;

		/* Save bus information */
		mDevice->BusInformation = PciDevice;

		/* Initial */
		mDevice->Driver.Name = NULL;
		mDevice->Driver.Version = -1;
		mDevice->Driver.Data = NULL;
		mDevice->Driver.Status = DriverNone;

		/* Register */
		DmCreateDevice((char*)PciToString(Pcs->Class, Pcs->Subclass, Pcs->Interface), mDevice);
	}

	/* Is this a secondary (PCI) bus */
	if ((Pcs->Class == PCI_DEVICE_CLASS_BRIDGE) && (Pcs->Subclass == PCI_DEVICE_SUBCLASS_PCI))
	{
		/* Uh oh, this dude has children */
		PciDevice->Children = list_create(LIST_SAFE);

		SecondBus = PciReadSecondaryBusNumber(BusIo, Bus, Device, Function);
		PciCheckBus(PciDevice->Children, BusIo, SecondBus);
	}
}

/* Check a device */
void PciCheckDevice(list_t *Bridge, PciBus_t *Bus, uint8_t BusNo, uint8_t Device)
{
	uint8_t Function = 0;
	uint16_t VendorId = 0;
	uint8_t HeaderType = 0;

	/* Get vendor id */
	VendorId = PciReadVendorId(Bus, BusNo, Device, Function);

	/* Sanity */
	if (VendorId == 0xFFFF)
		return;

	/* Check function 0 */
	PciCheckFunction(Bridge, Bus, BusNo, Device, Function);
	HeaderType = PciReadHeaderType(Bus, BusNo, Device, Function);

	/* Multi-function or single? */
	if (HeaderType & 0x80)
	{
		/* It is a multi-function device, so check remaining functions */
		for (Function = 1; Function < 8; Function++)
		{
			/* Only check if valid vendor */
			if (PciReadVendorId(Bus, BusNo, Device, Function) != 0xFFFF)
				PciCheckFunction(Bridge, Bus, BusNo, Device, Function);
		}
	}
}

/* Check a bus */
void PciCheckBus(list_t *Bridge, PciBus_t *Bus, uint8_t BusNo)
{
	uint8_t Device;

	for (Device = 0; Device < 32; Device++)
		PciCheckDevice(Bridge, Bus, BusNo, Device);
}

/* First of all, devices exists on TWO different
 * busses. PCI and PCI Express. */
MODULES_API void ModuleInit(void *Data)
{
	/* We need these */
	ACPI_TABLE_MCFG *McfgTable = (ACPI_TABLE_MCFG*)Data;
	uint32_t Function;
	uint32_t BusNo;
	uint8_t HeaderType;

	/* Init list, this is "bus 0" */
	GlbPciDevices = list_create(LIST_SAFE);

	/* Pci Express */
	if (McfgTable != NULL)
	{
		/* Woah, there exists Pci Express Controllers */
		uint32_t EntryCount = (McfgTable->Header.Length - sizeof(ACPI_TABLE_MCFG) / sizeof(McfgEntry_t));
		uint32_t Itr = 0;
		McfgEntry_t *Entry = (McfgEntry_t*)((uint8_t*)McfgTable + sizeof(ACPI_TABLE_MCFG));

		/* Iterate */
		for (Itr = 0; Itr < EntryCount; Itr++)
		{
			/* Allocate entry */
			PciBus_t *Bus = (PciBus_t*)kmalloc(sizeof(PciBus_t));

			/* Get size, which is the rest of the bytes */
			int PageCount = (1024 * 1024 * 256) / PAGE_SIZE;

			/* Memory Map 256 MB!!!!! Oh fucking god */
			Bus->IoSpace = IoSpaceCreate(DEVICE_IO_SPACE_MMIO, (Addr_t)Entry->BaseAddress, PageCount);
			Bus->IsExtended = 1;
			Bus->BusStart = Entry->StartBus;
			Bus->BusEnd = Entry->EndBus;
			Bus->Segment = Entry->SegmentGroup;

			/* Enumerate devices */
			for (Function = Bus->BusStart; Function <= Bus->BusEnd; Function++)
			{
				/* Check bus */
				BusNo = Function;
				PciCheckBus(GlbPciDevices, Bus, (uint8_t)BusNo);
			}

			/* Next */
			Entry++;
		}
	}
	else
	{
		/* Allocate entry */
		PciBus_t *Bus = (PciBus_t*)kmalloc(sizeof(PciBus_t));

		/* Setup */
		Bus->BusStart = 0;
		Bus->BusEnd = 7;
		Bus->IoSpace = IoSpaceCreate(DEVICE_IO_SPACE_IO, X86_PCI_SELECT, 8);
		Bus->IsExtended = 0;
		Bus->Segment = 0;

		/* Pci Legacy */
		HeaderType = PciReadHeaderType(Bus, 0, 0, 0);

		if ((HeaderType & 0x80) == 0)
		{
			/* Single PCI host controller */
			PciCheckBus(GlbPciDevices, Bus, 0);
		}
		else
		{
			/* Multiple PCI host controllers */
			for (Function = 0; Function < 8; Function++)
			{
				if (PciReadVendorId(Bus, 0, 0, Function) != 0xFFFF)
					break;

				/* Check bus */
				BusNo = Function;
				PciCheckBus(GlbPciDevices, Bus, (uint8_t)BusNo);
			}
		}
	}
}