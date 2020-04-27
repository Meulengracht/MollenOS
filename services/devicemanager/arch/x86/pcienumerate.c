/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * X86 Bus Driver 
 * - Enumerates the bus and registers the devices/controllers
 *   available in the system
 */
//#define __TRACE

#include <assert.h>
#include "../../devicemanager.h"
#include "bus.h"
#include <ddk/acpi.h>
#include <ddk/busdevice.h>
#include <ddk/interrupt.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <errno.h>

/* PCI-Express Support
 * This is the acpi-mcfg entry structure that represents an pci-express controller */
PACKED_TYPESTRUCT(McfgEntry, {
    uint64_t BaseAddress;
    uint16_t SegmentGroup;
    uint8_t  StartBus;
    uint8_t  EndBus;
    uint32_t Reserved;
});

/* Globals, we want to
 * keep track of all pci-devices by having this root device */
static Collection_t *__GlbPciDevices    = NULL;
static PciDevice_t *__GlbRoot           = NULL;
static int __GlbAcpiAvailable           = 0;

/* Prototypes
 * we need access to this function again.. */
void PciCheckBus(PciDevice_t *Parent, int Bus);

/*
static uint32_t pci_mmcfg_readreg(uint32_t bdf, uint8_t reg)
{
    return virtbase[(bdf << 10) | (reg >> 2)];
}

static uint32_t pci_mmcfg_writereg(uint32_t bdf, uint8_t reg, uint32_t val)
{
    virtbase[(bdf << 10) | (reg >> 2)] = val;
}
*/

/* PciToDevClass
 * Helper to construct the class from available pci-information */
unsigned int PciToDevClass(uint32_t Class, uint32_t SubClass) {
    return ((Class & 0xFFFF) << 16 | (SubClass & 0xFFFF));
}

/* PciToDevSubClass
 * Helper to construct the sub-class from available pci-information */
unsigned int PciToDevSubClass(uint32_t Interface) {
    return ((Interface & 0xFFFF) << 16 | 0);
}

/* PciValidateBarSize
 * Validates the size of a bar and the validity of the bar-size */
uint64_t
PciValidateBarSize(
    _In_ uint64_t Base,
    _In_ uint64_t MaxBase,
    _In_ uint64_t Mask)
{
    uint64_t Size = Mask & MaxBase;

    if (!Size) {
        return 0;
    }
    Size = (Size & ~(Size - 1)) - 1;

    if (Base == MaxBase && ((Base | Size) & Mask) != Mask) {
        return 0;
    }
    else {
        return Size;
    }
}

/* PciReadBars
 * Reads and initializes all available bars for the given pci-device */
void
PciReadBars(
    _In_ PciBus_t*    Bus,
    _In_ BusDevice_t* Device,
    _In_ uint32_t     HeaderType)
{
    // Buses have 2 io spaces, devices have 6
    int Count = (HeaderType & 0x1) == 0x1 ? 2 : 6;
    int i;

    /* Iterate all the avilable bars */
    for (i = 0; i < Count; i++) {
        uint32_t Space32, Size32, Mask32;
        uint64_t Space64, Size64, Mask64;
        size_t Offset = 0x10 + (i << 2);

        // Calculate the initial mask 
        Mask32 = (HeaderType & 0x1) == 0x1 ? ~0x7FF : 0xFFFFFFFF;

        // Read both space and size
        Space32 = PciRead32(Bus, Device->Bus, Device->Slot, Device->Function, Offset);
        PciWrite32(Bus, Device->Bus, Device->Slot, Device->Function, Offset, Space32 | Mask32);
        Size32 = PciRead32(Bus, Device->Bus, Device->Slot, Device->Function, Offset);
        PciWrite32(Bus, Device->Bus, Device->Slot, Device->Function, Offset, Space32);

        // Sanitize bounds of values
        if (Size32 == 0xFFFFFFFF) {
            Size32 = 0;
        }
        if (Space32 == 0xFFFFFFFF) {
            Space32 = 0;
        }

        // Which kind of io-space is it, if bit 0 is set, it's io and not mmio 
        if (Space32 & 0x1) {
            // Update mask to reflect IO space
            Mask64  = 0xFFFC;
            Size64  = Size32;
            Space64 = Space32 & 0xFFFC;

            // Correctly update the size of the io
            Size64 = PciValidateBarSize(Space64, Size64, Mask64);
            if (Space64 != 0 && Size64 != 0) {
                CreateDevicePortIo(&Device->IoSpaces[i], LOWORD(Space64), (size_t)Size64);
            }
        }
        // Ok, its memory, but is it 64 bit or 32 bit? 
        // Bit 2 is set for 64 bit memory space
        else if (Space32 & 0x4) {
            Space64 = Space32 & 0xFFFFFFF0;
            Size64  = Size32 & 0xFFFFFFF0;
            Mask64  = 0xFFFFFFFFFFFFFFF0;
            
            // Calculate a new 64 bit offset
            i++;
            Offset = 0x10 + (i << 2);

            // Read both space and size for 64 bit
            Space32 = PciRead32(Bus, Device->Bus, Device->Slot, Device->Function, Offset);
            PciWrite32(Bus, Device->Bus, Device->Slot, Device->Function, Offset, 0xFFFFFFFF);
            Size32 = PciRead32(Bus, Device->Bus, Device->Slot, Device->Function, Offset);
            PciWrite32(Bus, Device->Bus, Device->Slot, Device->Function, Offset, Space32);

            // Set the upper 32 bit of the space
            Space64 |= ((uint64_t)Space32 << 32);
            Size64  |= ((uint64_t)Size32 << 32);
#if defined(i386) || defined(__i386__)
            if (sizeof(uintptr_t) < 8 && Space64 > SIZE_MAX) {
                WARNING("Found 64 bit device with 64 bit address, can't use it in 32 bit mode");
                return;
            }
#endif

            // Correct the size and validate
            Size64 = PciValidateBarSize(Space64, Size64, Mask64);
            if (Space64 != 0 && Size64 != 0) {
                CreateDeviceMemoryIo(&Device->IoSpaces[i], (uintptr_t)Space64, (size_t)Size64);
            }
        }
        else {
            Space64 = Space32 & 0xFFFFFFF0;
            Size64  = Size32 & 0xFFFFFFF0;
            Mask64  = 0xFFFFFFF0;

            // Correct the size and validate
            Size64 = PciValidateBarSize(Space64, Size64, Mask64);
            if (Space64 != 0 && Size64 != 0) {
                CreateDeviceMemoryIo(&Device->IoSpaces[i], (uintptr_t)Space64, (size_t)Size64);
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
 * Create a new pci-device from a valid bus/device/function location on the bus */
void
PciCheckFunction(
    _In_ PciDevice_t* Parent, 
    _In_ int          Bus, 
    _In_ int          Slot, 
    _In_ int          Function)
{
    // Variables
    PciNativeHeader_t *Pcs  = NULL;
    PciDevice_t *Device     = NULL;
    int SecondBus           = 0;
    DataKey_t lKey;

    // Allocate new instances of both the pci-header information
    // and the pci-device structure
    Pcs     = (PciNativeHeader_t*)malloc(sizeof(PciNativeHeader_t));
    Device  = (PciDevice_t*)malloc(sizeof(PciDevice_t));
    assert(Pcs != NULL);
    assert(Device != NULL);

    // Read entire function information
    PciReadFunction(Pcs, Parent->BusIo, (unsigned int)Bus, (unsigned int)Slot, (unsigned int)Function);

    Device->Parent      = Parent;
    Device->BusIo       = Parent->BusIo;
    Device->Header      = Pcs;
    Device->Bus         = Bus;
    Device->Slot        = Slot;
    Device->Function    = Function;
    Device->Children    = NULL;
    Device->AcpiConform = 0;

    // Trace Information about device 
    // Ignore the spam of device_id 0x7a0 in VMWare
    // This is VIRTIO devices
    if (Pcs->DeviceId != 0x7a0) {
        TRACE(" - [%x:%x:%x] %s", Bus, Device, Function,
            PciToString(Pcs->Class, Pcs->Subclass, Pcs->Interface));
    }

    // Do some disabling, but NOT on the video or bridge
    if ((Pcs->Class != PCI_CLASS_BRIDGE)
        && (Pcs->Class != PCI_CLASS_VIDEO)) {
        uint16_t PciSettings = PciRead16(Device->BusIo, Bus, Slot, Function, 0x04);
        PciWrite16(Device->BusIo, Bus, Slot, Function, 0x04, PciSettings | PCI_COMMAND_INTDISABLE);
    }

    // Add to the flat list
    lKey.Value.Integer = 0;
    CollectionAppend(__GlbPciDevices, CollectionCreateNode(lKey, Device));

    // Add to list
    if (Pcs->Class == PCI_CLASS_BRIDGE && Pcs->Subclass == PCI_BRIDGE_SUBCLASS_PCI) {
        Device->IsBridge = 1;
        lKey.Value.Integer = 1;
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
            PciDevice_t* Iterator      = Device;
            Flags_t      AcpiConform   = 0;
            int          InterruptLine = INTERRUPT_NONE;
            int          Pin           = Pcs->InterruptPin;

            // Sanitize legals
            if (Pin > 4) {
                Pin = 1;
            }

            // Does device even use interrupts?
            if (Pin != 0) {
                // Swizzle till we reach root
                // Case 1 - Query device for ACPI filter
                //        -> 1.1: It has an routing for our Dev/Pin
                //             -> Exit
                //          -> 1.2: It does not have an routing
                //           -> Swizzle-pin
                //           -> Get parent device
                //           -> Go-To 1
                while (Iterator && Iterator != __GlbRoot) {
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
                    PciWrite8(Parent->BusIo, (unsigned int)Bus, (unsigned int)Slot,
                        (unsigned int)Function, 0x3C, (uint8_t)InterruptLine);
                    Device->Header->InterruptLine = (uint8_t)InterruptLine;
                    Device->AcpiConform           = AcpiConform;
                }
            }
        }

        // Set keys and type
        Device->IsBridge    = 0;
        lKey.Value.Integer  = 0;
        CollectionAppend(Parent->Children, CollectionCreateNode(lKey, Device));
    }
}

/* PciCheckDevice
 * Checks if there is any connection on the given
 * pci-location, and enumerates it's function if available */
void
PciCheckDevice(
    _In_ PciDevice_t* Parent, 
    _In_ int          Bus, 
    _In_ int          Slot)
{
    // Variables
    uint16_t VendorId   = 0;
    int Function        = 0;

    // Validate the vendor id, it's invalid only
    // if there is no device on that location
    VendorId = PciReadVendorId(Parent->BusIo, 
        (unsigned int)Bus, (unsigned int)Slot, (unsigned int)Function);

    // Sanitize if device is present
    if (VendorId == 0xFFFF) {
        return;
    }

    // Check base function
    PciCheckFunction(Parent, Bus, Slot, Function);

    // Multi-function or single? 
    // If it is a multi-function device, check remaining functions
    if (PciReadHeaderType(Parent->BusIo, 
        (unsigned int)Bus, (unsigned int)Slot, (unsigned int)Function) & 0x80) {
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
    _In_ PciDevice_t* Parent, 
    _In_ int          Bus)
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

/* CreateBusDeviceFromPciDevice
 * Creates a new Device_t from a pci-device and registers it with the device-manager */
static OsStatus_t
CreateBusDeviceFromPciDevice(
    _In_ PciDevice_t* PciDevice)
{
    BusDevice_t Device = { { 0 } };

    Device.Base.Length   = sizeof(BusDevice_t);
    Device.Base.VendorId = PciDevice->Header->VendorId;
    Device.Base.DeviceId = PciDevice->Header->DeviceId;
    Device.Base.Class    = PciToDevClass(PciDevice->Header->Class, PciDevice->Header->Subclass);
    Device.Base.Subclass = PciToDevSubClass(PciDevice->Header->Interface);

    Device.Segment  = (unsigned int)PciDevice->BusIo->Segment;
    Device.Bus      = PciDevice->Bus;
    Device.Slot     = PciDevice->Slot;
    Device.Function = PciDevice->Function;

    Device.InterruptLine        = (int)PciDevice->Header->InterruptLine;
    Device.InterruptPin         = (int)PciDevice->Header->InterruptPin;
    Device.InterruptAcpiConform = PciDevice->AcpiConform;

    // Handle bars attached to device
    PciReadBars(PciDevice->BusIo, &Device, PciDevice->Header->HeaderType);

    // PCI - IDE Bar Fixup
    // From experience ide-bars don't always show up (ex: Oracle VM and Bochs)
    // but only the initial 4 bars don't, the BM bar
    // always seem to show up 
    if (PciDevice->Header->Class == PCI_CLASS_STORAGE
        && PciDevice->Header->Subclass == PCI_STORAGE_SUBCLASS_IDE) {
        if ((PciDevice->Header->Interface & 0x1) == 0) {
            if (Device.IoSpaces[0].Type == DeviceIoInvalid) {
                CreateDevicePortIo(&Device.IoSpaces[0], 0x1F0, 8);
            }
            if (Device.IoSpaces[1].Type == DeviceIoInvalid) {
                CreateDevicePortIo(&Device.IoSpaces[1], 0x3F6, 4);
            }
        }
        if ((PciDevice->Header->Interface & 0x4) == 0) {
            if (Device.IoSpaces[2].Type == DeviceIoInvalid) {
                CreateDevicePortIo(&Device.IoSpaces[2], 0x170, 8);
            }
            if (Device.IoSpaces[3].Type == DeviceIoInvalid) {
                CreateDevicePortIo(&Device.IoSpaces[3], 0x376, 4);
            }
        }
    }
    return DmRegisterDevice(UUID_INVALID, &Device.Base,
        PciToString(PciDevice->Header->Class,
        PciDevice->Header->Subclass, PciDevice->Header->Interface),
        DEVICE_REGISTER_FLAG_LOADDRIVER, &Device.Base.Id);
}

/* PciInstallDriverCallback
 * Enumerates all found pci-devices in our list and loads drivers for the them */
void
PciInstallDriverCallback(
    _In_     void* Data, 
    _In_     int   No, 
    _In_Opt_ void* Context)
{
    PciDevice_t *PciDev = (PciDevice_t*)Data;
    _CRT_UNUSED(No);

    // Bridge or device? 
    // If a bridge, we keep iterating, device, load driver
    if (PciDev->IsBridge) {
        CollectionExecuteAll(PciDev->Children, PciInstallDriverCallback, Context);
    }
    else {
        CreateBusDeviceFromPciDevice(PciDev);
    }
}

/* BusInstallFixed
 * Loads a fixed driver for the vendorid/deviceid */
OsStatus_t
BusInstallFixed(
    _In_ BusDevice_t* Device,
    _In_ const char*  Name)
{
    // Set some magic constants
    Device->Base.Length   = sizeof(BusDevice_t);
    Device->Base.VendorId = PCI_FIXED_VENDORID;

    // Set more magic constants to ignore class and subclass
    Device->Base.Class    = 0xFF0F;
    Device->Base.Subclass = 0xFF0F;

    // Invalidate irqs, this must be set by fixed drivers
    Device->InterruptPin         = INTERRUPT_NONE;
    Device->InterruptLine        = INTERRUPT_NONE;
    Device->InterruptAcpiConform = 0;
    return DmRegisterDevice(UUID_INVALID, &Device->Base, Name,  
        DEVICE_REGISTER_FLAG_LOADDRIVER, &Device->Base.Id);
}

/* BusRegisterPS2Controller
 * Loads a fixed driver for the vendorid/deviceid */
OsStatus_t
BusRegisterPS2Controller(void)
{
    BusDevice_t Device = { { 0 } };
    OsStatus_t  Status;

    // Set default ps2 device settings
    Device.Base.DeviceId = PCI_PS2_DEVICEID;

    // Register io-spaces for the ps2 controller, it has two ports
    // Data port - 0x60
    // Status/Command port - 0x64
    // one byte each
    Status = CreateDevicePortIo(&Device.IoSpaces[0], 0x60, 1);
    if (Status != OsSuccess) {
        ERROR(" > failed to initialize ps2 data io space");
        return OsError;
    }

    Status = CreateDevicePortIo(&Device.IoSpaces[1], 0x64, 1);
    if (Status != OsSuccess) {
        ERROR(" > failed to initialize ps2 command/status io space");
        return OsError;
    }
    return BusInstallFixed(&Device, "PS/2 Controller");
}

/* BusEnumerate
 * Enumerates the pci-bus, on newer pcs its possbile for 
 * devices exists on TWO different busses. PCI and PCI Express. */
int
BusEnumerate(void* Context)
{
    ACPI_TABLE_MCFG*   McfgTable = NULL;
    ACPI_TABLE_HEADER* Header    = NULL;
    AcpiDescriptor_t   Acpi      = { 0 };
    OsStatus_t         Status;
    int                Function;

    _CRT_UNUSED(Context);

    __GlbRoot = (PciDevice_t*)malloc(sizeof(PciDevice_t));
    if (!__GlbRoot) {
        return ENOMEM;
    }
    memset(__GlbRoot, 0, sizeof(PciDevice_t));

    // Initialize a flat list of devices
    __GlbPciDevices     = CollectionCreate(KeyInteger);
    __GlbRoot->Children = CollectionCreate(KeyInteger);
    __GlbRoot->IsBridge = 1;

    // Are we on an acpi-capable system?
    if (AcpiQueryStatus(&Acpi) == OsSuccess) {
        TRACE("ACPI-Version: 0x%x (BootFlags 0x%x)", 
            Acpi.Version, Acpi.BootFlags);
        __GlbAcpiAvailable = 1;

        // Uh, even better, do we have PCI-e controllers?
        if (AcpiQueryTable(ACPI_SIG_MCFG, &Header) == OsSuccess) {
            TRACE("PCI-Express Controller (mcfg length 0x%x)", Header->Length);
            //McfgTable = (ACPI_TABLE_MCFG*)Header;
            //remember to free(McfgTable)
            free(Header);
        }

        // Check for PS2 controller presence
        if (Acpi.BootFlags & ACPI_IA_8042 || Acpi.BootFlags == 0) {
            BusRegisterPS2Controller();
        }
    }
    else {
        // We can pretty much assume all 8042 devices
        // are present in system, like PS2, etc
        BusRegisterPS2Controller();
    }
    
    // @todo lookup mcfg table

    // If the mcfg table is present we have pci-e controllers onboard.
    if (McfgTable != NULL) {
        McfgEntry_t *Entry  = (McfgEntry_t*)((uint8_t*)McfgTable + sizeof(ACPI_TABLE_MCFG));
        int EntryCount      = (McfgTable->Header.Length - sizeof(ACPI_TABLE_MCFG) / sizeof(McfgEntry_t));
        int i;

        for (i = 0; i < EntryCount; i++) {
            PciBus_t *Bus = (PciBus_t*)malloc(sizeof(PciBus_t));
            memset(Bus, 0, sizeof(PciBus_t));

            if (CreateDeviceMemoryIo(&Bus->IoSpace, (uintptr_t)Entry->BaseAddress, (1024 * 1024 * 256)) != OsSuccess) {
                ERROR(" > failed to create pcie address space");
                return ENODEV;
            }

            // PCIe memory spaces spand up to 256 mb
            Bus->IsExtended     = 1;
            Bus->BusStart       = Entry->StartBus;
            Bus->BusEnd         = Entry->EndBus;
            Bus->Segment        = Entry->SegmentGroup;
            __GlbRoot->BusIo    = Bus;

            for (Function = Bus->BusStart; Function <= Bus->BusEnd; Function++) {
                PciCheckBus(__GlbRoot, Function);
            }
            Entry++;
        }
        free(McfgTable);
    }
    else {
        // Otherwise we have traditional PCI buses
        PciBus_t *Bus = (PciBus_t*)malloc(sizeof(PciBus_t));
        if (!Bus) {
            return ENOMEM;
        }
        memset(Bus, 0, sizeof(PciBus_t));

        __GlbRoot->BusIo = Bus;
        Bus->BusEnd      = 7;
        
        // PCI buses use io
        Status = CreateDevicePortIo(&Bus->IoSpace, PCI_IO_BASE, PCI_IO_LENGTH);
        if (Status != OsSuccess) {
            ERROR(" > failed to initialize pci io space");
            return ENODEV;
        }

        Status = AcquireDeviceIo(&Bus->IoSpace);
        if (Status != OsSuccess) {
            ERROR(" > failed to acquire pci io space");
            return ENODEV;
        }

        // We can check whether or not it's a multi-function
        // root-bridge, in that case there are multiple buses
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
    CollectionExecuteAll(__GlbRoot->Children, PciInstallDriverCallback, NULL);
    return EOK;
}

OsStatus_t
DmIoctlDevice(
    _In_ BusDevice_t* Device,
    _In_ unsigned int Command,
    _In_ unsigned int Flags)
{
    PciDevice_t* PciDevice = NULL;
    uint16_t     Settings;

    // Lookup pci-device
    foreach(dNode, __GlbPciDevices) {
        PciDevice_t* Entry = (PciDevice_t*)dNode->Data;
        if (Entry->Bus          == Device->Bus
            && Entry->Slot      == Device->Slot
            && Entry->Function  == Device->Function) {
            PciDevice = Entry;
            break;
        }
    }

    // Sanitize
    if (PciDevice == NULL) {
        ERROR(" > failed to locate pci-device for ioctl");
        return OsError;
    }

    // Read value, modify and write back
    Settings = PciRead16(PciDevice->BusIo, Device->Bus, Device->Slot, Device->Function, 0x04);

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
    if (Flags & __DEVICEMANAGER_IOCTL_IO_ENABLE) {
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
    PciWrite16(PciDevice->BusIo, Device->Bus, Device->Slot, Device->Function, 0x04, Settings);
    return OsSuccess;
}

OsStatus_t
DmIoctlDeviceEx(
	_In_ BusDevice_t* Device,
	_In_ int          Direction,
	_In_ unsigned int Register,
	_In_ size_t*      Value,
	_In_ size_t       Width)
{
    PciDevice_t *PciDevice = NULL;

    // Lookup pci-device
    foreach(dNode, __GlbPciDevices) {
        PciDevice_t *Entry = (PciDevice_t*)dNode->Data;
        if (Entry->Bus             == Device->Bus
            && Entry->Slot         == Device->Slot
            && Entry->Function     == Device->Function) {
            PciDevice = Entry;
            break;
        }
    }

    if (PciDevice == NULL) {
        ERROR(" > failed to locate pci-device for ioctl");
        return OsError;
    }

    if (Direction == __DEVICEMANAGER_IOCTL_EXT_READ) {
        if (Width == 1) {
            *Value = (size_t)PciRead8(PciDevice->BusIo, Device->Bus,
                Device->Slot, Device->Function, Register);
        }
        else if (Width == 2) {
            *Value = (size_t)PciRead16(PciDevice->BusIo, Device->Bus,
                Device->Slot, Device->Function, Register);
        }
        else if (Width == 4) {
            *Value = (size_t)PciRead32(PciDevice->BusIo, Device->Bus,
                Device->Slot, Device->Function, Register);
        }
        else {
            return OsInvalidParameters;
        }
    }
    else {
        if (Width == 1) {
            PciWrite8(PciDevice->BusIo, Device->Bus, 
                Device->Slot, Device->Function, Register, LOBYTE(Value));
        }
        else if (Width == 2) {
            PciWrite16(PciDevice->BusIo, Device->Bus, 
                Device->Slot, Device->Function, Register, LOWORD(Value));
        }
        else if (Width == 4) {
            PciWrite32(PciDevice->BusIo, Device->Bus, 
                Device->Slot, Device->Function, Register, LODWORD(Value));
        }
        else {
            return OsInvalidParameters;
        }
    }
    return OsSuccess;
}
