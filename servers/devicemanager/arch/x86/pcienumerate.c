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
#include <os/mollenos.h>
#include "bus.h"

/* Includes
 * - Library */
#include <stdlib.h>
#include <stddef.h>

/* PCI-Express Support
 * This is the acpi-mcfg entry structure that
 * represents an pci-express controller */
#pragma pack(push, 1)
typedef struct _McfgEntry {
	uint64_t			BaseAddress;
	uint16_t			SegmentGroup;
	uint8_t				StartBus;
	uint8_t				EndBus;
	uint32_t			Reserved;
} McfgEntry_t; 
#pragma pack(pop)

/* Globals, we want to
 * keep track of all pci-devices by having this root device */
PciDevice_t *GlbRootBridge = NULL;

/* Prototypes
 * we need access to this function again.. */
void PciCheckBus(PciDevice_t *Parent, int Bus);

/* PciToDevClass
 * Helper to construct the class from
 * available pci-information */
DevInfo_t PciToDevClass(uint32_t Class, uint32_t SubClass)
{
	return ((Class & 0xFFFF) << 16 | SubClass & 0xFFFF);
}

/* PciToDevSubClass
 * Helper to construct the sub-class from
 * available pci-information */
DevInfo_t PciToDevSubClass(uint32_t Interface)
{
	return ((Interface & 0xFFFF) << 16 | 0);
}

/* PciSetIoSpace
 * Helper function to construct a new
 * io-space that uses static memory */
void PciSetIoSpace(MCoreDevice_t *Device, int Index, int Type, Addr_t Base, size_t Length)
{
	Device->IoSpaces[Index].Type = Type;
	Device->IoSpaces[Index].PhysicalBase = Base;
	Device->IoSpaces[Index].Size = Length;
}

/* PciValidateBarSize
 * Validates the size of a bar and the validity of
 * the bar-size */
uint64_t PciValidateBarSize(uint64_t Base, uint64_t MaxBase, uint64_t Mask)
{
	/* Find the significant bits */
	uint64_t Size = Mask & MaxBase;

	/* Sanity */
	if (!Size) {
		return 0;
	}

	/* Calc correct size */
	Size = (Size & ~(Size - 1)) - 1;

	/* Sanitize the base */
	if (Base == MaxBase && ((Base | Size) & Mask) != Mask) {
		return 0;
	}
	else {
		return Size;
	}
}

/* PciReadBars
 * Reads and initializes all available bars for the 
 * given pci-device */
void PciReadBars(PciBus_t *Bus, MCoreDevice_t *Device, uint32_t HeaderType)
{
	/* Variables, initialize count to
	 * either 2 or 6 depending on header type */
	int Count = (HeaderType & 0x1) == 0x1 ? 2 : 6;
	int i;

	/* Iterate all the avilable bars */
	for (i = 0; i < Count; i++)
	{
		/* Variables for initializing bars */
		size_t Offset = 0x10 + (i << 2);
		uint32_t Space32, Size32, Mask32;
		uint64_t Space64, Size64, Mask64;

		/* Calc initial mask */
		Mask32 = (HeaderType & 0x1) == 0x1 ? ~0x7FF : 0xFFFFFFFF;

		/* Read all the bar information */
		Space32 = PciRead32(Bus, Device->Bus, Device->Device, Device->Function, Offset);
		PciWrite32(Bus, Device->Bus, Device->Device, Device->Function, Offset, Space32 | Mask32);
		Size32 = PciRead32(Bus, Device->Bus, Device->Device, Device->Function, Offset);
		PciWrite32(Bus, Device->Bus, Device->Device, Device->Function, Offset, Space32);

		/* Sanitize the size and space values */
		if (Size32 == 0xFFFFFFFF) {
			Size32 = 0;
		}
		if (Space32 == 0xFFFFFFFF) {
			Space32 = 0;
		}

		/* Which kind of io-space is it,
		 * if bit 0 is set, it's io and not mmio */
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
		/* Ok, its memory, but is it 64 bit or 32 bit? 
		 * Bit 2 is set for 64 bit memory space */
		else if (Space32 & 0x4) 
		{
			/* 64 Bit */
			Space64 = Space32 & 0xFFFFFFF0;
			Size64 = Size32 & 0xFFFFFFF0;
			Mask64 = 0xFFFFFFFFFFFFFFF0;
			
			/* Calculate new offset */
			i++;
			Offset = 0x10 + (i << 2);

			/* Get the size of the spaces */
			Space32 = PciRead32(Bus, Device->Bus, Device->Device, Device->Function, Offset);
			PciWrite32(Bus, Device->Bus, Device->Device, Device->Function, Offset, 0xFFFFFFFF);
			Size32 = PciRead32(Bus, Device->Bus, Device->Device, Device->Function, Offset);
			PciWrite32(Bus, Device->Bus, Device->Device, Device->Function, Offset, Space32);

			/* Add it to our current */
			Space64 |= ((uint64_t)Space32 << 32);
			Size64 |= ((uint64_t)Size32 << 32);

			/* Sanitize the space */
			if (sizeof(Addr_t) < 8 && Space64 > SIZE_MAX) {
				MollenOSSystemLog("Found 64 bit device with 64 bit address, can't use it in 32 bit mode");
				return;
			}

			/* Correct the size */
			Size64 = PciValidateBarSize(Space64, Size64, Mask64);

			/* Create */
			if (Space64 != 0 && Size64 != 0) {
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

/* PciCheckFunction
 * Create a new pci-device from a valid
 * bus/device/function location on the bus */
void PciCheckFunction(PciDevice_t *Parent, int Bus, int Device, int Function)
{
	/* Vars */
	PciDevice_t *PciDevice = NULL;
	PciNativeHeader_t *Pcs = NULL;
	int SecondBus = 0;
	DataKey_t lKey;

	/* Allocate new instances of both the pci-header information
	 * and the pci-device structure */
	Pcs = (PciNativeHeader_t*)malloc(sizeof(PciNativeHeader_t));
	PciDevice = (PciDevice_t*)malloc(sizeof(PciDevice_t));

	/* Read entire function information */
	PciReadFunction(Pcs, Parent->BusIo, 
		(DevInfo_t)Bus, (DevInfo_t)Device, (DevInfo_t)Function);

	/* Set initial stuff */
	PciDevice->Parent = Parent;
	PciDevice->BusIo = Parent->BusIo;
	PciDevice->Header = Pcs;
	PciDevice->Bus = Bus;
	PciDevice->Device = Device;
	PciDevice->Function = Function;
	PciDevice->Children = NULL;

	/* Info 
	 * Ignore the spam of device_id 0x7a0 in VMWare*/
#ifdef PCI_DIAGNOSE
	if (Pcs->DeviceId != 0x7a0) {
		MollenOSSystemLog("    * [%d:%d:%d][%d:%d:%d] Vendor 0x%x, Device 0x%x : %s\n",
			Pcs->Class, Pcs->Subclass, Pcs->Interface,
			Bus, Device, Function,
			Pcs->VendorId, Pcs->DeviceId,
			PciToString(Pcs->Class, Pcs->Subclass, Pcs->Interface));
	}
#endif

	/* Do some disabling, but NOT on the video or bridge */
	if ((Pcs->Class != PCI_CLASS_BRIDGE)
		&& (Pcs->Class != PCI_CLASS_VIDEO)) {
		uint16_t PciSettings = PciRead16(PciDevice->BusIo, Bus, Device, Function, 0x04);
		PciWrite16(PciDevice->BusIo, Bus, Device, Function, 0x04,
			PciSettings | PCI_COMMAND_INTDISABLE);
	}
	
	/* Add to list */
	if (Pcs->Class == PCI_CLASS_BRIDGE
		&& Pcs->Subclass == PCI_BRIDGE_SUBCLASS_PCI) {
		PciDevice->IsBridge = 1;
		lKey.Value = 1;
		ListAppend((List_t*)Parent->Children, ListCreateNode(lKey, lKey, PciDevice));

		/* Uh oh, this dude has children */
		PciDevice->Children = ListCreate(KeyInteger, LIST_NORMAL);

		/* Get secondary bus no and iterate */
		SecondBus = PciReadSecondaryBusNumber(PciDevice->BusIo, Bus, Device, Function);
		PciCheckBus(PciDevice, SecondBus);
	}
	else {
		PciDevice->IsBridge = 0;
		lKey.Value = 0;
		ListAppend((List_t*)Parent->Children, ListCreateNode(lKey, lKey, PciDevice));
	}
}

/* PciCheckDevice
 * Checks if there is any connection on the given
 * pci-location, and enumerates it's function if available */
void PciCheckDevice(PciDevice_t *Parent, int Bus, int Device)
{
	/* Variables */
	uint16_t VendorId = 0;
	int Function = 0;

	/* Validate the vendor id, it's invalid only
	 * if there is no device on that location */
	VendorId = PciReadVendorId(Parent->BusIo, 
		(DevInfo_t)Bus, (DevInfo_t)Device, (DevInfo_t)Function);

	/* Sanitize */
	if (VendorId == 0xFFFF) {
		return;
	}

	/* Check base function */
	PciCheckFunction(Parent, Bus, Device, Function);

	/* Multi-function or single? 
	 * If it is a multi-function device, check remaining functions */
	if (PciReadHeaderType(Parent->BusIo, 
		(DevInfo_t)Bus, (DevInfo_t)Device, (DevInfo_t)Function) & 0x80) {
		for (Function = 1; Function < 8; Function++) {
			if (PciReadVendorId(Parent->BusIo, Bus, Device, Function) != 0xFFFF) {
				PciCheckFunction(Parent, Bus, Device, Function);
			}
		}
	}
}

/* PciCheckBus
 * Enumerates all possible devices on the given bus */
void PciCheckBus(PciDevice_t *Parent, int Bus)
{
	/* Variables */
	int Device;

	/* Sanitize parameters */
	if (Parent == NULL || Bus < 0) {
		return;
	}

	/* Iterate all possible 32 devices on the pci-bus */
	for (Device = 0; Device < 32; Device++) {
		PciCheckDevice(Parent, Bus, Device);
	}
}

/* PciCreateDeviceFromPci
 * Creates a new MCoreDevice_t from a pci-device 
 * and registers it with the device-manager */
void PciCreateDeviceFromPci(PciDevice_t *PciDev)
{
	/* Allocate a new instance of the device structure */
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

	/* Register */
	RegisterDevice(mDevice, PciToString(PciDev->Header->Class, 
		PciDev->Header->Subclass, PciDev->Header->Interface));
}

/* PciInstallDriverCallback
 * Enumerates all found pci-devices in our list
 * and loads drivers for the them */
void PciInstallDriverCallback(void *Data, int No, void *Context)
{
	/* Initialize a pci-dev pointer */
	PciDevice_t *PciDev = (PciDevice_t*)Data;
	_CRT_UNUSED(No);

	/* Bridge or device? 
	 * If a bridge, we keep iterating, device, load driver */
	if (PciDev->IsBridge) {
		ListExecuteAll((List_t*)PciDev->Children, PciInstallDriverCallback, Context);
	}
	else {
		PciCreateDeviceFromPci(PciDev);
	}
}

/* BusInstallFixed
 * Loads a fixed driver for the vendorid/deviceid */
void BusInstallFixed(DevInfo_t DeviceId, const char *Name)
{
	/* Allocate a new instance of the device structure */
	MCoreDevice_t *Device = (MCoreDevice_t*)malloc(sizeof(MCoreDevice_t));
	memset(Device, 0, sizeof(MCoreDevice_t));

	/* Update parameters */
	Device->VendorId = PCI_FIXED_VENDORID;
	Device->DeviceId = DeviceId;

	/* Invalidate generics */
	Device->Class = 0xFF0F;
	Device->Subclass = 0xFF0F;

	/* Invalidate irqs */
	Device->IrqPin = -1;
	Device->IrqLine = -1;
	Device->IrqAvailable[0] = -1;

	/* Install driver */
	RegisterDevice(Device, Name);
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

	/* Initialize the root bridge element */
	GlbRootBridge = (PciDevice_t*)malloc(sizeof(PciDevice_t));
	memset(GlbRootBridge, 0, sizeof(PciDevice_t));

	/* Set some initial vars */
	GlbRootBridge->Children = ListCreate(KeyInteger, LIST_NORMAL);
	GlbRootBridge->IsBridge = 1;

	/* Query acpi information */
	if (AcpiQueryStatus(&Acpi) == OsNoError) {
		MollenOSSystemLog("Acpi is available! Version 0x%x", Acpi.Version);

		/* PCI-Express */
		if (AcpiQueryTable(ACPI_SIG_MCFG, &Header) == OsNoError) {
			MollenOSSystemLog("Found PCIe controller (mcfg length 0x%x)", Header->Length);
			//McfgTable = (ACPI_TABLE_MCFG*)Header;
			//remember to free(McfgTable)
			free(Header);
		}

		/* HPET */
		if (AcpiQueryTable(ACPI_SIG_HPET, &Header) == OsNoError) {
			MollenOSSystemLog("Found hpet");
			free(Header);
			BusInstallFixed(PCI_HPET_DEVICEID, "HPET Controller");
		}
		else {
			/* Install PIT if no RTC */
			if (!(Acpi.BootFlags & ACPI_IA_NO_CMOS_RTC)) {
				BusInstallFixed(PCI_PIT_DEVICEID, "ISA-PIT");
			}
		}

		/* Boot up cmos */
		BusInstallFixed(PCI_CMOS_RTC_DEVICEID, "CMOS/RTC Controller");

		/* Does the PS2 exist in our system? */
		if (Acpi.BootFlags & ACPI_IA_8042) {
			BusInstallFixed(PCI_PS2_DEVICEID, "PS/2 Controller");
		}
	}
	else {
		/* We can pretty much assume all 8042 devices
		 * are present in system, like RTC, PS2, etc */
		BusInstallFixed(PCI_CMOS_RTC_DEVICEID, "CMOS/RTC Controller");
		BusInstallFixed(PCI_PIT_DEVICEID, "ISA-PIT");
		BusInstallFixed(PCI_PS2_DEVICEID, "PS/2 Controller");
	}

	/* Pci Express */
	if (McfgTable != NULL)
	{
		/* Woah, there exists Pci Express Controllers */
		McfgEntry_t *Entry = (McfgEntry_t*)((uint8_t*)McfgTable + sizeof(ACPI_TABLE_MCFG));
		size_t EntryCount = (McfgTable->Header.Length - sizeof(ACPI_TABLE_MCFG) / sizeof(McfgEntry_t));
		size_t Itr = 0;

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
				PciCheckBus(GlbRootBridge, Function);
			}

			/* Next */
			Entry++;
		}

		/* Cleanup the mcfg table */
		free(McfgTable);
	}
	else
	{
		/* Allocate a new pci-bus controller */
		PciBus_t *Bus = (PciBus_t*)malloc(sizeof(PciBus_t));
		memset(Bus, 0, sizeof(PciBus_t));

		/* Store the newly allocated bus in root bridge */
		GlbRootBridge->BusIo = Bus;

		/* Setup some initial stuff */
		Bus->BusEnd = 7;
		
		/* Initialize a fixed io-space */
		Bus->IoSpace.Type = IO_SPACE_IO;
		Bus->IoSpace.PhysicalBase = PCI_IO_BASE;
		Bus->IoSpace.Size = PCI_IO_LENGTH;
		
		/* Register the io-space with the system */
		if (CreateIoSpace(&Bus->IoSpace) != OsNoError) {
			MollenOSSystemLog("Failed to initialize bus io");
			for (;;);
		}

		/* Now we acquire the io-space */
		if (AcquireIoSpace(&Bus->IoSpace) != OsNoError) {
			MollenOSSystemLog("Failed to acquire bus io with id %u", Bus->IoSpace.Id);
			for (;;);
		}
		
		/* We can check whether or not it's a multi-function
		 * root-bridge, in that case there are multiple buses */
		if (!(PciReadHeaderType(Bus, 0, 0, 0) & 0x80)) {
			PciCheckBus(GlbRootBridge, 0);
		}
		else {
			for (Function = 0; Function < 8; Function++) {
				if (PciReadVendorId(Bus, 0, 0, Function) != 0xFFFF)
					break;
				PciCheckBus(GlbRootBridge, Function);
			}
		}
	}

	/* Now, that the bus is enumerated, we can
	 * iterate the found devices and load drivers */
	ListExecuteAll(GlbRootBridge->Children, PciInstallDriverCallback, NULL);
}
