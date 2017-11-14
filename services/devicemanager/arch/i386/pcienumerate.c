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
//#define __TRACE

/* Includes 
 * - System */
#include <os/driver/device.h>
#include <os/driver/acpi.h>
#include <os/utils.h>
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
static Collection_t *__GlbPciDevices    = NULL;
static PciDevice_t *__GlbRoot           = NULL;
static int __GlbAcpiAvailable           = 0;

/* Prototypes
 * we need access to this function again.. */
void PciCheckBus(PciDevice_t *Parent, int Bus);

/* PciToDevClass
 * Helper to construct the class from
 * available pci-information */
DevInfo_t PciToDevClass(uint32_t Class, uint32_t SubClass) {
	return ((Class & 0xFFFF) << 16 | (SubClass & 0xFFFF));
}

/* PciToDevSubClass
 * Helper to construct the sub-class from
 * available pci-information */
DevInfo_t PciToDevSubClass(uint32_t Interface) {
	return ((Interface & 0xFFFF) << 16 | 0);
}

/* PciSetIoSpace
 * Helper function to construct a new
 * io-space that uses static memory */
void
PciSetIoSpace(
	_In_ MCoreDevice_t *Device, 
	_In_ int Index, 
	_In_ int Type, 
	_In_ uintptr_t Base, 
	_In_ size_t Length)
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
		Space32 = PciRead32(Bus, Device->Bus, Device->Slot, Device->Function, Offset);
		PciWrite32(Bus, Device->Bus, Device->Slot, Device->Function, Offset, Space32 | Mask32);
		Size32 = PciRead32(Bus, Device->Bus, Device->Slot, Device->Function, Offset);
		PciWrite32(Bus, Device->Bus, Device->Slot, Device->Function, Offset, Space32);

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
				PciSetIoSpace(Device, i, IO_SPACE_IO, (uintptr_t)Space64, (size_t)Size64);
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
			Space32 = PciRead32(Bus, Device->Bus, Device->Slot, Device->Function, Offset);
			PciWrite32(Bus, Device->Bus, Device->Slot, Device->Function, Offset, 0xFFFFFFFF);
			Size32 = PciRead32(Bus, Device->Bus, Device->Slot, Device->Function, Offset);
			PciWrite32(Bus, Device->Bus, Device->Slot, Device->Function, Offset, Space32);

			/* Add it to our current */
			Space64 |= ((uint64_t)Space32 << 32);
			Size64 |= ((uint64_t)Size32 << 32);

			/* Sanitize the space */
			if (sizeof(uintptr_t) < 8 && Space64 > SIZE_MAX) {
				WARNING("Found 64 bit device with 64 bit address, can't use it in 32 bit mode");
				return;
			}

			/* Correct the size */
			Size64 = PciValidateBarSize(Space64, Size64, Mask64);

			/* Create */
			if (Space64 != 0 && Size64 != 0) {
				PciSetIoSpace(Device, i, IO_SPACE_MMIO, (uintptr_t)Space64, (size_t)Size64);
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
				PciSetIoSpace(Device, i, IO_SPACE_MMIO, (uintptr_t)Space64, (size_t)Size64);
			}
		}
	}
}

/* PciDerivePin
 * Pin conversion from behind a bridge */
int PciDerivePin(int Device, int Pin) {
	return (((Pin - 1) + Device) % 4) + 1;
}

/* PciCheckFunction
 * Create a new pci-device from a valid
 * bus/device/function location on the bus */
void
PciCheckFunction(
	_In_ PciDevice_t *Parent, 
	_In_ int Bus, 
	_In_ int Slot, 
	_In_ int Function)
{
	// Variables
	PciDevice_t *Device = NULL;
	PciNativeHeader_t *Pcs = NULL;
	int SecondBus = 0;
	DataKey_t lKey;

	// Allocate new instances of both the pci-header information
	// and the pci-device structure
	Pcs = (PciNativeHeader_t*)malloc(sizeof(PciNativeHeader_t));
	Device = (PciDevice_t*)malloc(sizeof(PciDevice_t));

	// Read entire function information
	PciReadFunction(Pcs, Parent->BusIo, 
		(DevInfo_t)Bus, (DevInfo_t)Slot, (DevInfo_t)Function);

	// Set initial stuff 
	Device->Parent = Parent;
	Device->BusIo = Parent->BusIo;
	Device->Header = Pcs;
	Device->Bus = Bus;
	Device->Slot = Slot;
	Device->Function = Function;
	Device->Children = NULL;
	Device->AcpiConform = 0;

	// Trace Information about device 
	// Ignore the spam of device_id 0x7a0 in VMWare
	if (Pcs->DeviceId != 0x7a0) {
		TRACE(" - [%d:%d:%d] %s", Bus, Device, Function,
			PciToString(Pcs->Class, Pcs->Subclass, Pcs->Interface));
	}

	// Do some disabling, but NOT on the video or bridge
	if ((Pcs->Class != PCI_CLASS_BRIDGE)
		&& (Pcs->Class != PCI_CLASS_VIDEO)) {
		uint16_t PciSettings = PciRead16(Device->BusIo, Bus, Slot, Function, 0x04);
		PciWrite16(Device->BusIo, Bus, Slot, Function, 0x04,
			PciSettings | PCI_COMMAND_INTDISABLE);
	}

	// Add to the flat list
	lKey.Value = 0;
	CollectionAppend(__GlbPciDevices, CollectionCreateNode(lKey, Device));

	// Add to list
	if (Pcs->Class == PCI_CLASS_BRIDGE
		&& Pcs->Subclass == PCI_BRIDGE_SUBCLASS_PCI) {
		Device->IsBridge = 1;
		lKey.Value = 1;
		CollectionAppend(Parent->Children, CollectionCreateNode(lKey, Device));
		Device->Children = CollectionCreate(KeyInteger);

		// Extract secondary bus
		SecondBus = PciReadSecondaryBusNumber(Device->BusIo, Bus, Slot, Function);
		PciCheckBus(Device, SecondBus);
	}
	else {
		// Trace
		TRACE("  * Initial Line %u, Pin %i", Pcs->InterruptLine, Pcs->InterruptPin);

		// We do need acpi for this 
		// query acpi interrupt information for device
		if (__GlbAcpiAvailable == 1) {
			PciDevice_t *Iterator = Device;
			Flags_t AcpiConform = 0;
			int InterruptLine = -1;
			int Pin = Pcs->InterruptPin;

			// Sanitize legals
			if (Pin > 4) {
				Pin = 1;
			}

			// Does device even use interrupts?
			if (Pin != 0) {
				// Swizzle till we reach root
				// Case 1 - Query device for ACPI filter
				//        -> 1.1: It has an routing for our Dev/Pin
				//			 -> Exit
				//	      -> 1.2: It does not have an routing
				//           -> Swizzle-pin
				//           -> Get parent device
				//           -> Go-To 1
				while (Iterator != __GlbRoot) {
					OsStatus_t HasFilter = AcpiQueryInterrupt(
						Iterator->Bus, Iterator->Slot, Pin, 
						&InterruptLine, &AcpiConform);

					// Did routing exist?
					if (HasFilter == OsSuccess) {
						TRACE("  * Final Line %u - Final Pin %i", InterruptLine, Pin);
						break;
					}

					// Nope, swizzle pin, move up the ladder
					Pin = PciDerivePin((int)Iterator->Slot, Pin);
					Iterator = Iterator->Parent;

					// Trace
					TRACE("  * Derived Pin %i", Pin);
				}

				// Update the irq-line if we found a new line
				if (InterruptLine != INTERRUPT_NONE) {
					PciWrite8(Parent->BusIo, (DevInfo_t)Bus, (DevInfo_t)Slot,
						(DevInfo_t)Function, 0x3C, (uint8_t)InterruptLine);
					Device->Header->InterruptLine = (uint8_t)InterruptLine;
					Device->AcpiConform = AcpiConform;
				}
			}
		}

		// Set keys and type
		Device->IsBridge = 0;
		lKey.Value = 0;

		// Add to list
		CollectionAppend(Parent->Children, CollectionCreateNode(lKey, Device));
	}
}

/* PciCheckDevice
 * Checks if there is any connection on the given
 * pci-location, and enumerates it's function if available */
void
PciCheckDevice(
	_In_ PciDevice_t *Parent, 
	_In_ int Bus, 
	_In_ int Slot)
{
	// Variables
	uint16_t VendorId = 0;
	int Function = 0;

	// Validate the vendor id, it's invalid only
	// if there is no device on that location
	VendorId = PciReadVendorId(Parent->BusIo, 
		(DevInfo_t)Bus, (DevInfo_t)Slot, (DevInfo_t)Function);

	// Sanitize if device is present
	if (VendorId == 0xFFFF) {
		return;
	}

	// Check base function
	PciCheckFunction(Parent, Bus, Slot, Function);

	// Multi-function or single? 
	// If it is a multi-function device, check remaining functions
	if (PciReadHeaderType(Parent->BusIo, 
		(DevInfo_t)Bus, (DevInfo_t)Slot, (DevInfo_t)Function) & 0x80) {
		for (Function = 1; Function < 8; Function++) {
			if (PciReadVendorId(Parent->BusIo, Bus, Slot, Function) != 0xFFFF) {
				PciCheckFunction(Parent, Bus, Slot, Function);
			}
		}
	}
}

/* PciCheckBus
 * Enumerates all possible devices on the given bus */
void
PciCheckBus(
	_In_ PciDevice_t *Parent, 
	_In_ int Bus)
{
	// Variables
	int Device;

	// Sanitize parameters
	if (Parent == NULL || Bus < 0) {
		return;
	}

	// Iterate all possible 32 devices on the pci-bus
	for (Device = 0; Device < 32; Device++) {
		PciCheckDevice(Parent, Bus, Device);
	}
}

/* PciCreateDeviceFromPci
 * Creates a new MCoreDevice_t from a pci-device 
 * and registers it with the device-manager */
OsStatus_t
PciCreateDeviceFromPci(
	_In_ PciDevice_t *PciDevice)
{
	// Variables
	MCoreDevice_t Device;

	// Zero out structure
    memset(&Device, 0, sizeof(MCoreDevice_t));
    Device.Length = sizeof(MCoreDevice_t);
	Device.VendorId = PciDevice->Header->VendorId;
	Device.DeviceId = PciDevice->Header->DeviceId;
	Device.Class = PciToDevClass(PciDevice->Header->Class, PciDevice->Header->Subclass);
	Device.Subclass = PciToDevSubClass(PciDevice->Header->Interface);

	Device.Segment = (DevInfo_t)PciDevice->BusIo->Segment;
	Device.Bus = PciDevice->Bus;
	Device.Slot = PciDevice->Slot;
	Device.Function = PciDevice->Function;

	Device.Interrupt.Line = (int)PciDevice->Header->InterruptLine;
	Device.Interrupt.Pin = (int)PciDevice->Header->InterruptPin;
	Device.Interrupt.Vectors[0] = INTERRUPT_NONE;
	Device.Interrupt.AcpiConform = PciDevice->AcpiConform;

	// Handle bars attached to device
	PciReadBars(PciDevice->BusIo, &Device, PciDevice->Header->HeaderType);

	// PCI - IDE Bar Fixup
	// From experience ide-bars don't always show up (ex: Oracle VM)
	// but only the initial 4 bars don't, the BM bar
	// always seem to show up 
	if (PciDevice->Header->Class == PCI_CLASS_STORAGE
		&& PciDevice->Header->Subclass == PCI_STORAGE_SUBCLASS_IDE) {
		if ((PciDevice->Header->Interface & 0x1) == 0) {
			if (Device.IoSpaces[0].Type == IO_SPACE_INVALID) {
				PciSetIoSpace(&Device, 0, IO_SPACE_IO, 0x1F0, 8);
			}
			if (Device.IoSpaces[1].Type == IO_SPACE_INVALID) {
				PciSetIoSpace(&Device, 1, IO_SPACE_IO, 0x3F6, 4);
			}
		}
		if ((PciDevice->Header->Interface & 0x4) == 0) {
			if (Device.IoSpaces[2].Type == IO_SPACE_INVALID) {
				PciSetIoSpace(&Device, 2, IO_SPACE_IO, 0x170, 8);
			}
			if (Device.IoSpaces[3].Type == IO_SPACE_INVALID) {
				PciSetIoSpace(&Device, 3, IO_SPACE_IO, 0x376, 4);
			}
		}
	}

	// Register the device
	return RegisterDevice(UUID_INVALID, &Device, PciToString(PciDevice->Header->Class,
		PciDevice->Header->Subclass, PciDevice->Header->Interface),
		__DEVICEMANAGER_REGISTER_LOADDRIVER, &Device.Id);
}

/* PciInstallDriverCallback
 * Enumerates all found pci-devices in our list
 * and loads drivers for the them */
void
PciInstallDriverCallback(
    _In_ void *Data, 
    _In_ int No, 
    _In_Opt_ void *Context)
{
	// Variables
	PciDevice_t *PciDev = (PciDevice_t*)Data;
	_CRT_UNUSED(No);

	// Bridge or device? 
	// If a bridge, we keep iterating, device, load driver
	if (PciDev->IsBridge) {
		CollectionExecuteAll(PciDev->Children, PciInstallDriverCallback, Context);
	}
	else {
		PciCreateDeviceFromPci(PciDev);
	}
}

/* BusInstallFixed
 * Loads a fixed driver for the vendorid/deviceid */
OsStatus_t
BusInstallFixed(
	_In_ DevInfo_t DeviceId,
	_In_ __CONST char *Name)
{
	// Variables
	MCoreDevice_t Device;

	// Zero out structure
	memset(&Device, 0, sizeof(MCoreDevice_t));

    // Set some magic constants
    Device.Length = sizeof(MCoreDevice_t);
	Device.VendorId = PCI_FIXED_VENDORID;
	Device.DeviceId = DeviceId;

	// Set more magic constants to ignore class and subclass
	Device.Class = 0xFF0F;
	Device.Subclass = 0xFF0F;

	// Invalidate irqs, this must be set by fixed drivers
	Device.Interrupt.Pin = INTERRUPT_NONE;
	Device.Interrupt.Line = INTERRUPT_NONE;
	Device.Interrupt.Vectors[0] = INTERRUPT_NONE;
	Device.Interrupt.AcpiConform = 0;

	// Install the driver
	return RegisterDevice(UUID_INVALID, &Device, Name, 
		__DEVICEMANAGER_REGISTER_LOADDRIVER, &Device.Id);
}

/* BusEnumerate
 * Enumerates the pci-bus, on newer pcs its possbile for 
 * devices exists on TWO different busses. PCI and PCI Express. */
OsStatus_t
BusEnumerate(void)
{
	// Variables
	ACPI_TABLE_HEADER *Header = NULL;
	ACPI_TABLE_MCFG *McfgTable = NULL;
	AcpiDescriptor_t Acpi;
	int Function;

	// Initialize the root bridge element
	__GlbRoot = (PciDevice_t*)malloc(sizeof(PciDevice_t));
	memset(__GlbRoot, 0, sizeof(PciDevice_t));

	// Initialize a flat list of devices
	__GlbPciDevices = CollectionCreate(KeyInteger);

	// Initiate root-bridge
	__GlbRoot->Children = CollectionCreate(KeyInteger);
	__GlbRoot->IsBridge = 1;

	/* Query acpi information */
	if (AcpiQueryStatus(&Acpi) == OsSuccess) {
		TRACE("ACPI-Version: 0x%x (BootFlags 0x%x)", 
			Acpi.Version, Acpi.BootFlags);
		__GlbAcpiAvailable = 1;

		/* PCI-Express */
		if (AcpiQueryTable(ACPI_SIG_MCFG, &Header) == OsSuccess) {
			TRACE("PCI-Express Controller (mcfg length 0x%x)", Header->Length);
			//McfgTable = (ACPI_TABLE_MCFG*)Header;
			//remember to free(McfgTable)
			free(Header);
		}

		/* HPET */
		if (AcpiQueryTable(ACPI_SIG_HPET, &Header) == OsSuccess) {
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
		if (Acpi.BootFlags & ACPI_IA_8042
			|| Acpi.BootFlags == 0) {
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
			Bus->IoSpace.PhysicalBase = (uintptr_t)Entry->BaseAddress;
			Bus->IoSpace.Size = (1024 * 1024 * 256);

			/* Memory Map 256 MB!!!!! Oh fucking god */
			Bus->IsExtended = 1;
			Bus->BusStart = Entry->StartBus;
			Bus->BusEnd = Entry->EndBus;
			Bus->Segment = Entry->SegmentGroup;

			/* Store bus */
			__GlbRoot->BusIo = Bus;

			/* Enumerate devices */
			for (Function = Bus->BusStart; Function <= Bus->BusEnd; Function++) {
				PciCheckBus(__GlbRoot, Function);
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
		__GlbRoot->BusIo = Bus;

		/* Setup some initial stuff */
		Bus->BusEnd = 7;
		
		/* Initialize a fixed io-space */
		Bus->IoSpace.Type = IO_SPACE_IO;
		Bus->IoSpace.PhysicalBase = PCI_IO_BASE;
		Bus->IoSpace.Size = PCI_IO_LENGTH;
		
		/* Register the io-space with the system */
		if (CreateIoSpace(&Bus->IoSpace) != OsSuccess) {
			ERROR("Failed to initialize bus io");
			for (;;);
		}

		/* Now we acquire the io-space */
		if (AcquireIoSpace(&Bus->IoSpace) != OsSuccess) {
			ERROR("Failed to acquire bus io with id %u", Bus->IoSpace.Id);
			for (;;);
		}
		
		/* We can check whether or not it's a multi-function
		 * root-bridge, in that case there are multiple buses */
		if (!(PciReadHeaderType(Bus, 0, 0, 0) & 0x80)) {
			PciCheckBus(__GlbRoot, 0);
		}
		else {
			for (Function = 0; Function < 8; Function++) {
				if (PciReadVendorId(Bus, 0, 0, Function) != 0xFFFF)
					break;
				PciCheckBus(__GlbRoot, Function);
			}
		}
	}

	/* Now, that the bus is enumerated, we can
	 * iterate the found devices and load drivers */
	CollectionExecuteAll(__GlbRoot->Children, PciInstallDriverCallback, NULL);

	// No errors
	return OsSuccess;
}

/* IoctlDevice 
 * Performs any neccessary actions to control the device on the bus */
__EXTERN
OsStatus_t
IoctlDevice(
	_In_ MCoreDevice_t *Device,
	_In_ Flags_t Flags)
{
	// Variables
	PciDevice_t *PciDevice = NULL;
	uint16_t Settings;

	// Lookup pci-device
	foreach(dNode, __GlbPciDevices) {
		PciDevice_t *Entry = (PciDevice_t*)dNode->Data;
		if (Entry->Bus == Device->Bus
			&& Entry->Slot == Device->Slot
			&& Entry->Function == Device->Function) {
			PciDevice = Entry;
			break;
		}
	}

	// Sanitize
	if (PciDevice == NULL) {
		return OsError;
	}

	// Read value, modify and write back
	Settings = PciRead16(PciDevice->BusIo, Device->Bus,
		Device->Slot, Device->Function, 0x04);

	// Clear all possible flags first
	Settings &= ~(PCI_COMMAND_BUSMASTER | PCI_COMMAND_FASTBTB 
		| PCI_COMMAND_MMIO | PCI_COMMAND_PORTIO | PCI_COMMAND_INTDISABLE);

	// Handle enable
	if (!(Flags & __DEVICEMANAGER_IOCTL_ENABLE)) {
		Settings |= PCI_COMMAND_INTDISABLE;
	}

	// Handle io/mmio
	if (Flags & __DEVICEMANAGER_IOCTL_MMIO_ENABLE) {
		Settings |= PCI_COMMAND_MMIO;
	}
	else if (Flags & __DEVICEMANAGER_IOCTL_IO_ENABLE) {
		Settings |= PCI_COMMAND_PORTIO;
	}

	// Handle busmaster
	if (Flags & __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE) {
		Settings |= PCI_COMMAND_BUSMASTER;
	}

	// Handle fast-b2b
	if (Flags & __DEVICEMANAGER_IOCTL_FASTBTB_ENABLE) {
		Settings |= PCI_COMMAND_FASTBTB;
	}

	// Write back settings
	PciWrite16(PciDevice->BusIo, Device->Bus, 
		Device->Slot, Device->Function, 0x04, Settings);

	// Done
	return OsSuccess;
}

/* IoctlDeviceEx (Extended) 
 * Performs any neccessary actions to control the device on the bus */
Flags_t
IoctlDeviceEx(
	_In_ MCoreDevice_t *Device,
	_In_ Flags_t Parameters,
	_In_ Flags_t Register,
	_In_ Flags_t Value,
	_In_ size_t Width)
{
	// Variables
	PciDevice_t *PciDevice = NULL;

	// Lookup pci-device
	foreach(dNode, __GlbPciDevices) {
		PciDevice_t *Entry = (PciDevice_t*)dNode->Data;
		if (Entry->Bus == Device->Bus
			&& Entry->Slot == Device->Slot
			&& Entry->Function == Device->Function) {
			PciDevice = Entry;
			break;
		}
	}

	// Sanitize
	if (PciDevice == NULL) {
		return OsError;
	}

	// Which kind of action?
	if (Parameters & __DEVICEMANAGER_IOCTL_EXT_READ) {
		if (Width == 1) {
			return PciRead8(PciDevice->BusIo, Device->Bus,
				Device->Slot, Device->Function, Register);
		}
		else if (Width == 2) {
			return PciRead16(PciDevice->BusIo, Device->Bus,
				Device->Slot, Device->Function, Register);
		}
		else {
			return PciRead32(PciDevice->BusIo, Device->Bus,
				Device->Slot, Device->Function, Register);
		}
	}
	else {
		if (Width == 1) {
			PciWrite8(PciDevice->BusIo, Device->Bus, 
				Device->Slot, Device->Function, Register, Value);
		}
		else if (Width == 2) {
			PciWrite16(PciDevice->BusIo, Device->Bus, 
				Device->Slot, Device->Function, Register, Value);
		}
		else {
			PciWrite32(PciDevice->BusIo, Device->Bus, 
				Device->Slot, Device->Function, Register, Value);
		}
	
	}

	// Done
	return OsSuccess;
}
