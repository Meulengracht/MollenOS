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
 * MollenOS X86 Bus Driver 
 * - Enumerates the bus and registers the devices/controllers
 *   available in the system
 */

/* Includes 
 * - System */
#include <os/driver/device.h>
#include <os/driver/acpi.h>
#include "bus.h"

/* Includes
 * - Library */
#include <stdlib.h>
#include <stddef.h>

/* The MCFG Entry */
#pragma pack(push, 1)
typedef struct _McfgEntry {
	uint64_t			BaseAddress;
	uint16_t			SegmentGroup;
	uint8_t				StartBus;
	uint8_t				EndBus;
	uint32_t			Reserved;
} McfgEntry_t; 
#pragma pack(pop)

/* Globals */
PciDevice_t *GlbRootBridge = NULL;

/* Prototypes */
void PciCheckBus(PciDevice_t *Parent, uint8_t BusNo);

/* Create class information from dev */
DevInfo_t PciToDevClass(uint32_t Class, uint32_t SubClass)
{
	return ((Class & 0xFFFF) << 16 | SubClass & 0xFFFF);
}

DevInfo_t PciToDevSubClass(uint32_t Interface)
{
	return ((Interface & 0xFFFF) << 16 | 0);
}

void PciSetIoSpace(MCoreDevice_t *Device, int Index, int Type, Addr_t Base, size_t Length)
{
	Device->IoSpaces[Index].Type = Type;
	Device->IoSpaces[Index].PhysicalBase = Base;
	Device->IoSpaces[Index].Size = Length;
}

/* Validate size */
uint64_t PciValidateBarSize(uint64_t Base, uint64_t MaxBase, uint64_t Mask)
{
	/* Find the significant bits */
	uint64_t Size = Mask & MaxBase;

	/* Sanity */
	if (!Size)
		return 0;

	/* Calc correct size */
	Size = (Size & ~(Size - 1)) - 1;

	/* Sanitize the base */
	if (Base == MaxBase && ((Base | Size) & Mask) != Mask)
		return 0;

	/* Done! */
	return Size;
}

/* Read IO Areas */
void PciReadBars(PciBus_t *Bus, MCoreDevice_t *Device, uint32_t HeaderType)
{
	/* Sanity */
	int Count = (HeaderType & 0x1) == 0x1 ? 2 : 6;
	int i;

	/* Iterate */
	for (i = 0; i < Count; i++)
	{
		/* Vars */
		uint32_t Offset = 0x10 + (i << 2);
		uint32_t Space32, Size32, Mask32;
		uint64_t Space64, Size64, Mask64;

		/* Calc initial mask */
		Mask32 = (HeaderType & 0x1) == 0x1 ? ~0x7FF : 0xFFFFFFFF;

		/* Read */
		Space32 = PciRead32(Bus, Device->Bus, Device->Device, Device->Function, Offset);
		PciWrite32(Bus, Device->Bus, Device->Device, Device->Function, Offset, Space32 | Mask32);
		Size32 = PciRead32(Bus, Device->Bus, Device->Device, Device->Function, Offset);
		PciWrite32(Bus, Device->Bus, Device->Device, Device->Function, Offset, Space32);

		/* Sanity */
		if (Size32 == 0xFFFFFFFF)
			Size32 = 0;
		if (Space32 == 0xFFFFFFFF)
			Space32 = 0;

		/* Io? */
		if (Space32 & 0x1) 
		{
			/* Modify mask */
			Mask64 = 0xFFFC;
			Size64 = Size32;
			Space64 = Space32 & 0xFFFC;

			/* Correct the size */
			Size64 = PciValidateBarSize(Space64, Size64, Mask64);

			/* Sanity */
			if (Space64 != 0 && Size64 != 0) {
				PciSetIoSpace(Device, i, IO_SPACE_IO, (Addr_t)Space64, (size_t)Size64);
			}
		}

		/* Memory, 64 bit or 32 bit? */
		else if (Space32 & 0x4) {

			/* 64 Bit */
			Space64 = Space32 & 0xFFFFFFF0;
			Size64 = Size32 & 0xFFFFFFF0;
			Mask64 = 0xFFFFFFFFFFFFFFF0;
			
			/* Calculate new offset */
			i++;
			Offset = 0x10 + (i << 2);

			/* Read */
			Space32 = PciRead32(Bus, Device->Bus, Device->Device, Device->Function, Offset);
			PciWrite32(Bus, Device->Bus, Device->Device, Device->Function, Offset, 0xFFFFFFFF);
			Size32 = PciRead32(Bus, Device->Bus, Device->Device, Device->Function, Offset);
			PciWrite32(Bus, Device->Bus, Device->Device, Device->Function, Offset, Space32);

			/* Add */
			Space64 |= ((uint64_t)Space32 << 32);
			Size64 |= ((uint64_t)Size32 << 32);

			/* Sanity */
			if (sizeof(Addr_t) < 8
				&& Space64 > SIZE_MAX) {
				//LogFatal("PCIE", "Found 64 bit device with 64 bit address, can't use it in 32 bit mode");
				return;
			}

			/* Correct the size */
			Size64 = PciValidateBarSize(Space64, Size64, Mask64);

			/* Create */
			if (Space64 != 0
				&& Size64 != 0) {
				PciSetIoSpace(Device, i, IO_SPACE_MMIO, (Addr_t)Space64, (size_t)Size64);
			}
		}
		else {
			/* Set mask */
			Space64 = Space32 & 0xFFFFFFF0;
			Size64 = Size32 & 0xFFFFFFF0;
			Mask64 = 0xFFFFFFF0;

			/* Correct the size */
			Size64 = PciValidateBarSize(Space64, Size64, Mask64);

			/* Create */
			if (Space64 != 0
				&& Size64 != 0) {
				PciSetIoSpace(Device, i, IO_SPACE_MMIO, (Addr_t)Space64, (size_t)Size64);
			}
		}
	}
}

/* Check a function */
/* For each function we create a 
 * pci_device and add it to the list */
void PciCheckFunction(PciDevice_t *Parent, uint8_t Bus, uint8_t Device, uint8_t Function)
{
	/* Vars */
	uint8_t SecondBus;
	PciNativeHeader_t *Pcs;
	PciDevice_t *PciDevice;
	DataKey_t lKey;

	/* Allocate */
	Pcs = (PciNativeHeader_t*)malloc(sizeof(PciNativeHeader_t));
	PciDevice = (PciDevice_t*)malloc(sizeof(PciDevice_t));

	/* Get full information */
	PciReadFunction(Pcs, Parent->BusIo, Bus, Device, Function);

	/* Set information */
	PciDevice->Parent = Parent;
	PciDevice->BusIo = Parent->BusIo;
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
	if ((Pcs->Class != PCI_CLASS_BRIDGE)
		&& (Pcs->Class != PCI_CLASS_VIDEO))
	{
		/* Disable Device untill further notice */
		uint16_t PciSettings = PciRead16(PciDevice->BusIo, Bus, Device, Function, 0x04);
		PciWrite16(PciDevice->BusIo, Bus, Device, Function, 0x04,
			PciSettings | PCI_COMMAND_INTDISABLE);
	}
	
	/* Add to list */
	if (Pcs->Class == PCI_CLASS_BRIDGE
		&& Pcs->Subclass == PCI_BRIDGE_SUBCLASS_PCI)
	{
		PciDevice->IsBridge = 1;
		lKey.Value = 1;
		ListAppend((List_t*)Parent->Children, ListCreateNode(lKey, lKey, PciDevice));

		/* Uh oh, this dude has children */
		PciDevice->Children = ListCreate(KeyInteger, LIST_NORMAL);

		/* Get secondary bus no and iterate */
		SecondBus = PciReadSecondaryBusNumber(PciDevice->BusIo, Bus, Device, Function);
		PciCheckBus(PciDevice, SecondBus);
	}
	else
	{
		/* This is an device */
		PciDevice->IsBridge = 0;
		lKey.Value = 0;
		ListAppend((List_t*)Parent->Children, ListCreateNode(lKey, lKey, PciDevice));
	}
}

/* Check a device */
void PciCheckDevice(PciDevice_t *Parent, uint8_t Bus, uint8_t Device)
{
	uint8_t Function = 0;
	uint16_t VendorId = 0;
	uint8_t HeaderType = 0;

	/* Get vendor id */
	VendorId = PciReadVendorId(Parent->BusIo, Bus, Device, Function);

	/* Sanity */
	if (VendorId == 0xFFFF)
		return;

	/* Check function 0 */
	PciCheckFunction(Parent, Bus, Device, Function);
	HeaderType = PciReadHeaderType(Parent->BusIo, Bus, Device, Function);

	/* Multi-function or single? */
	if (HeaderType & 0x80)
	{
		/* It is a multi-function device, so check remaining functions */
		for (Function = 1; Function < 8; Function++)
		{
			/* Only check if valid vendor */
			if (PciReadVendorId(Parent->BusIo, Bus, Device, Function) != 0xFFFF)
				PciCheckFunction(Parent, Bus, Device, Function);
		}
	}
}

/* Check a bus */
void PciCheckBus(PciDevice_t *Parent, uint8_t BusNo)
{
	/* Vars */
	uint8_t Device;

	/* Iterate devices on bus */
	for (Device = 0; Device < 32; Device++)
		PciCheckDevice(Parent, BusNo, Device);
}

/* Convert PciDevice to MCoreDevice */
void PciCreateDeviceFromPci(PciDevice_t *PciDev)
{
	/* Register device with the OS */
	MCoreDevice_t *mDevice = (MCoreDevice_t*)malloc(sizeof(MCoreDevice_t));
	memset(mDevice, 0, sizeof(MCoreDevice_t));

	/* Setup information */
	mDevice->VendorId = PciDev->Header->VendorId;
	mDevice->DeviceId = PciDev->Header->DeviceId;
	mDevice->Class = PciToDevClass(PciDev->Header->Class, PciDev->Header->Subclass);
	mDevice->Subclass = PciToDevSubClass(PciDev->Header->Interface);

	mDevice->Segment = (DevInfo_t)PciDev->BusIo->Segment;
	mDevice->Bus = PciDev->Bus;
	mDevice->Device = PciDev->Device;
	mDevice->Function = PciDev->Function;

	mDevice->IrqLine = -1;
	mDevice->IrqPin = (int)PciDev->Header->InterruptPin;
	mDevice->IrqAvailable[0] = PciDev->Header->InterruptLine;

	/* Read Bars */
	PciReadBars(PciDev->BusIo, mDevice, PciDev->Header->HeaderType);

	/* PCI - IDE Bar Fixup
	 * From experience ide-bars don't always show up (ex: Oracle VM)
	 * but only the initial 4 bars don't, the BM bar
	 * always seem to show up */
	if (PciDev->Header->Class == PCI_CLASS_STORAGE
		&& PciDev->Header->Subclass == PCI_STORAGE_SUBCLASS_IDE)
	{
		/* Controller 1 */
		if ((PciDev->Header->Interface & 0x1) == 0) {
			if (mDevice->IoSpaces[0].Type == IO_SPACE_INVALID) 
				PciSetIoSpace(mDevice, 0, IO_SPACE_IO, 0x1F0, 8);
			if (mDevice->IoSpaces[1].Type == IO_SPACE_INVALID)
				PciSetIoSpace(mDevice, 1, IO_SPACE_IO, 0x3F6, 4);
		}

		/* Controller 2 */
		if ((PciDev->Header->Interface & 0x4) == 0) {
			if (mDevice->IoSpaces[2].Type == IO_SPACE_INVALID)
				PciSetIoSpace(mDevice, 2, IO_SPACE_IO, 0x170, 8);
			if (mDevice->IoSpaces[3].Type == IO_SPACE_INVALID)
				PciSetIoSpace(mDevice, 3, IO_SPACE_IO, 0x376, 4);
		}
	}

	/* Register 
	DmCreateDevice((char*)PciToString(PciDev->Header->Class,
		PciDev->Header->Subclass, PciDev->Header->Interface), mDevice); */
}

/* Install Driver Callback */
void PciInstallDriverCallback(void *Data, int No, void *Context)
{
	/* Cast */
	PciDevice_t *PciDev = (PciDevice_t*)Data;
	_CRT_UNUSED(No);

	/* Bridge or device? */
	if (PciDev->IsBridge) {
		ListExecuteAll((List_t*)PciDev->Children, PciInstallDriverCallback, Context);
	}
	else
	{
		/* Create device! */
		PciCreateDeviceFromPci(PciDev);
	}
}

/* BusEnumerate
 * Enumerates the pci-bus, on newer pcs its possbile for 
 * devices exists on TWO different busses. PCI and PCI Express. */
void BusEnumerate(void)
{
	/* We need these */
	AcpiDescriptor_t Acpi;
	ACPI_TABLE_HEADER *Header = NULL;
	ACPI_TABLE_MCFG *McfgTable = NULL;
	int Function;
	uint32_t BusNo;
	uint8_t HeaderType;

	/* Query acpi information */
	if (AcpiQueryStatus(&Acpi) == OsNoError) {
		/* Acpi is available! */
		if (AcpiQueryTable(ACPI_SIG_MCFG, &Header) == OsNoError) {
			/* Wuhu! */
			//McfgTable = (ACPI_TABLE_MCFG*)Header;
			//remember to free(McfgTable)
		}

		/* Check for things */
		if (!(Acpi.BootFlags & ACPI_IA_NO_CMOS_RTC)) {
			/* Boot up the CMOS and RTC */
			//LoadDriver(CMOS_AND_RTC) 
		}
		if (Acpi.BootFlags & ACPI_IA_8042) {
			/* Boot up PIT and PS2 */
			//LoadDriver(PIT)
			//LoadDriver(PS2)
		}
	}
	else {
		/* We can pretty much assume all 8042 devices
		 * are present in system, like RTC, PS2, etc */
		//LoadDriver(CMOS_AND_RTC) 
		//LoadDriver(PIT)
		//LoadDriver(PS2)
	}

	/* Init list, this is "bus 0" */
	GlbRootBridge = (PciDevice_t*)malloc(sizeof(PciDevice_t));
	GlbRootBridge->Children = ListCreate(KeyInteger, LIST_NORMAL);
	GlbRootBridge->Bus = 0;
	GlbRootBridge->Device = 0;
	GlbRootBridge->Function = 0;
	GlbRootBridge->Header = NULL;
	GlbRootBridge->Parent = NULL;

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
			PciBus_t *Bus = (PciBus_t*)malloc(sizeof(PciBus_t));
			memset(Bus, 0, sizeof(PciBus_t));

			/* Setup io-space */
			Bus->IoSpace.Type = IO_SPACE_MMIO;
			Bus->IoSpace.PhysicalBase = (Addr_t)Entry->BaseAddress;
			Bus->IoSpace.Size = (1024 * 1024 * 256);

			/* Memory Map 256 MB!!!!! Oh fucking god */
			Bus->IsExtended = 1;
			Bus->BusStart = Entry->StartBus;
			Bus->BusEnd = Entry->EndBus;
			Bus->Segment = Entry->SegmentGroup;

			/* Store bus */
			GlbRootBridge->BusIo = Bus;

			/* Enumerate devices */
			for (Function = Bus->BusStart; Function <= Bus->BusEnd; Function++) {
				BusNo = Function;
				PciCheckBus(GlbRootBridge, (uint8_t)BusNo);
			}

			/* Next */
			Entry++;
		}
	}
	else
	{
		/* Allocate entry */
		PciBus_t *Bus = (PciBus_t*)malloc(sizeof(PciBus_t));
		memset(Bus, 0, sizeof(PciBus_t));

		/* Setup */
		Bus->BusEnd = 7;
		
		/* Setup io-space */
		Bus->IoSpace.Type = IO_SPACE_IO;
		Bus->IoSpace.PhysicalBase = PCI_IO_BASE;
		Bus->IoSpace.Size = PCI_IO_LENGTH;

		/* Store bus */
		GlbRootBridge->BusIo = Bus;

		/* Pci Legacy */
		HeaderType = PciReadHeaderType(Bus, 0, 0, 0);

		if ((HeaderType & 0x80) == 0)
		{
			/* Single PCI host controller */
			PciCheckBus(GlbRootBridge, 0);
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
				PciCheckBus(GlbRootBridge, (uint8_t)BusNo);
			}
		}
	}

	/* Step 1. Enumerate bus */
	ListExecuteAll(GlbRootBridge->Children, PciInstallDriverCallback, NULL);
}
